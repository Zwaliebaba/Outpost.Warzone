#include "pch.h"
/*
 * Manifest.cpp
 *
 * Load GameData/datasets.json and drive the resource system from it.
 *
 * A unit is the faithful image of one former .wrf file: an ordered array of
 * {d, t, f} entries replayed as resSetDirectory/resLoadFile calls, so a unit
 * load makes exactly the calls the WRF parser used to make. A dataset is the
 * image of one GameDesc.lev block, built into the same LEVEL_DATASET shape
 * levParse produced - including the quirk that a camchange dataset shares
 * its name with the camstart it modifies.
 *
 * The texturePages table is the exception to "a unit entry is a file": a
 * TEXSET entry names a group of page bindings, which this file resolves
 * against the translucency setting and hands to Neuron::TextureCache.
 */

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

#include "Frame.h"
#include "FrameResource.h"
#include "ListMacs.h"
#include "Json.h"
#include "Levels.h"
#include "Manifest.h"
#include "TextureCache.h"
#include "WarzoneConfig.h"

namespace
{
  // the resident manifest document, and the unit index into it
  Neuron::Json g_document;
  bool g_loaded = false;
  std::unordered_map<std::string, const Neuron::Json*> g_units;

  /// "wrf\\Frontend.wrf" -> "wrf/frontend": lower case, forward slashes, no
  /// extension - the unit key the converter writes.
  std::string NormaliseUnitName(const STRING* _name)
  {
    std::string name(_name);
    for (char& c : name)
    {
      if (c == '\\')
        c = '/';
      else if (c >= 'A' && c <= 'Z')
        c = static_cast<char>(c - 'A' + 'a');
    }
    if (name.size() > 4 && name.compare(name.size() - 4, 4, ".wrf") == 0)
      name.resize(name.size() - 4);
    return name;
  }

  const Neuron::Json* FindUnit(const STRING* _name)
  {
    const auto it = g_units.find(NormaliseUnitName(_name));
    return it == g_units.end() ? nullptr : it->second;
  }

  /// The dataset kinds, spelled as the converter spells them.
  bool KindToType(const std::string& _kind, const Neuron::Json& _dataSet, SWORD& _outType)
  {
    if (_kind == "level") { _outType = LDS_COMPLETE; return true; }
    if (_kind == "campaign") { _outType = LDS_CAMPAIGN; return true; }
    if (_kind == "camstart") { _outType = LDS_CAMSTART; return true; }
    if (_kind == "camchange") { _outType = LDS_CAMCHANGE; return true; }
    if (_kind == "expand") { _outType = LDS_EXPAND; return true; }
    if (_kind == "between") { _outType = LDS_BETWEEN; return true; }
    if (_kind == "miss_keep") { _outType = LDS_MKEEP; return true; }
    if (_kind == "miss_clear") { _outType = LDS_MCLEAR; return true; }
    if (_kind == "expand_limbo") { _outType = LDS_EXPAND_LIMBO; return true; }
    if (_kind == "miss_keep_limbo") { _outType = LDS_MKEEP_LIMBO; return true; }
    if (_kind == "multi")
    {
      const Neuron::Json* type = _dataSet.Find("type");
      if (type == nullptr || !type->IsNumber())
        return false;
      _outType = static_cast<SWORD>(type->AsInt());
      return true;
    }
    return false;
  }

  /// The file a page uses out of its candidates. A page with one file uses
  /// it; a page with a "-hard"/"-soft" pair uses the one the translucency
  /// setting asks for. bufferTexPageLoad made the same test by skipping the
  /// entry it did not want.
  const std::string* ChooseVariant(const Neuron::Json& _files)
  {
    const std::string* only = nullptr;

    for (std::size_t i = 0; i < _files.Size(); i++)
    {
      if (!_files.Item(i).IsString())
        return nullptr;

      const std::string& file = _files.Item(i).AsString();
      std::string lowered = file;
      for (char& c : lowered)
        c = static_cast<char>(tolower(static_cast<unsigned char>(c)));

      const bool soft = lowered.find("soft") != std::string::npos;
      const bool hard = lowered.find("hard") != std::string::npos;

      if (war_GetAdditive() ? !soft : !hard)
        only = &file;
    }

    return only;
  }

  /// Flattens a group and everything it extends into ordered bindings.
  /// _depth guards a cycle in the data rather than trusting it.
  BOOL CollectGroup(const STRING* _group, std::vector<Neuron::TextureCache::PageBinding>& _out, int _depth)
  {
    if (_depth > 8)
    {
      Neuron::Fatal("texturePages: group {} extends itself", _group);
      return FALSE;
    }

    const Neuron::Json* groups = g_document.Find("texturePages");
    groups = groups == nullptr ? nullptr : groups->Find("groups");
    const Neuron::Json* group = groups == nullptr ? nullptr : groups->Find(_group);

    if (group == nullptr || !group->IsObject())
    {
      Neuron::Fatal("texturePages: no group named {}", _group);
      return FALSE;
    }

    if (const Neuron::Json* extends = group->Find("extends"))
    {
      if (!extends->IsString() || !CollectGroup(extends->AsString().c_str(), _out, _depth + 1))
        return FALSE;
    }

    const Neuron::Json* pages = group->Find("pages");
    if (pages == nullptr || !pages->IsArray())
    {
      Neuron::Fatal("texturePages: group {} has no page list", _group);
      return FALSE;
    }

    for (std::size_t i = 0; i < pages->Size(); i++)
    {
      const Neuron::Json& page = pages->Item(i);
      const Neuron::Json* id = page.Find("id");
      const Neuron::Json* files = page.Find("files");
      if (id == nullptr || !id->IsString() || files == nullptr || !files->IsArray() || files->Size() == 0)
      {
        Neuron::Fatal("texturePages: group {} page {} is malformed", _group, i);
        return FALSE;
      }

      const std::string* chosen = ChooseVariant(*files);
      if (chosen == nullptr)
      {
        Neuron::Fatal("texturePages: group {} page {} has no usable file", _group, id->AsString());
        return FALSE;
      }

      /* an extending group replaces the base's binding in place, so the
         order a page was first bound in is the order it keeps */
      const auto at = std::find_if(_out.begin(), _out.end(),
                                   [&](const Neuron::TextureCache::PageBinding& _b) { return _b.pageId == id->AsString(); });
      if (at != _out.end())
        at->file = *chosen;
      else
        _out.push_back({id->AsString(), *chosen});
    }

    return TRUE;
  }

  STRING* DupString(const std::string& _text)
  {
    auto pCopy = new (std::nothrow) STRING[_text.size() + 1];
    if (pCopy != nullptr)
      strcpy(pCopy, _text.c_str());
    return pCopy;
  }

  /// Build one LEVEL_DATASET from its manifest record.
  BOOL RegisterDataSet(const Neuron::Json& _dataSet)
  {
    const Neuron::Json* name = _dataSet.Find("name");
    const Neuron::Json* kind = _dataSet.Find("kind");
    if (name == nullptr || !name->IsString() || kind == nullptr || !kind->IsString())
    {
      Neuron::Fatal("ManifestRegisterDataSets: dataset without name or kind");
      return FALSE;
    }

    SWORD type;
    if (!KindToType(kind->AsString(), _dataSet, type))
    {
      Neuron::Fatal("ManifestRegisterDataSets: unknown kind {} for {}", kind->AsString(), name->AsString());
      return FALSE;
    }

    auto psDataSet = new (std::nothrow) LEVEL_DATASET[1];
    if (psDataSet == nullptr)
    {
      Neuron::Fatal("ManifestRegisterDataSets: out of memory");
      return FALSE;
    }
    memset(psDataSet, 0, sizeof(LEVEL_DATASET));
    psDataSet->type = type;
    psDataSet->players = 1;
    psDataSet->game = -1;

    if (const Neuron::Json* players = _dataSet.Find("players"))
      psDataSet->players = static_cast<SWORD>(players->AsInt());

    // a camchange modifies the camstart that carries the same name, and the
    // camstart is required to be declared first - levParse's rule, kept
    if (type == LDS_CAMCHANGE)
    {
      LEVEL_DATASET* psFoundData;
      if (!levFindDataSet(const_cast<STRING*>(name->AsString().c_str()), &psFoundData)
        || psFoundData->type != LDS_CAMSTART)
      {
        Neuron::Fatal("ManifestRegisterDataSets: camchange {} has no camstart", name->AsString());
        delete[] psDataSet;
        return FALSE;
      }
      psFoundData->psChange = psDataSet;
    }

    psDataSet->pName = DupString(name->AsString());

    if (const Neuron::Json* base = _dataSet.Find("base"))
    {
      if (!levFindDataSet(const_cast<STRING*>(base->AsString().c_str()), &psDataSet->psBaseData))
      {
        Neuron::Fatal("ManifestRegisterDataSets: {} names unknown base dataset {}", name->AsString(), base->AsString());
        return FALSE;
      }
    }

    const Neuron::Json* slots = _dataSet.Find("slots");
    const std::size_t slotCount = slots != nullptr ? slots->Size() : 0;
    if (slots == nullptr || !slots->IsArray() || slotCount > LEVEL_MAXFILES)
    {
      Neuron::Fatal("ManifestRegisterDataSets: {} has a bad slot list", name->AsString());
      return FALSE;
    }
    for (std::size_t i = 0; i < slotCount; i++)
    {
      const Neuron::Json& slot = slots->Item(i);
      if (const Neuron::Json* unit = slot.Find("unit"))
      {
        if (FindUnit(unit->AsString().c_str()) == nullptr)
        {
          Neuron::Fatal("ManifestRegisterDataSets: {} references unknown unit {}", name->AsString(), unit->AsString());
          return FALSE;
        }
        psDataSet->apDataFiles[i] = DupString(unit->AsString());
      }
      else if (const Neuron::Json* game = slot.Find("game"))
      {
        // the scenario path, spelled the way levParse left it: lower case
        // with backslash separators
        std::string path = game->AsString();
        for (char& c : path)
        {
          if (c == '/')
            c = '\\';
        }
        psDataSet->game = static_cast<SWORD>(i);
        psDataSet->apDataFiles[i] = DupString(path);
      }
      else
      {
        Neuron::Fatal("ManifestRegisterDataSets: {} slot {} is neither unit nor game", name->AsString(), i);
        return FALSE;
      }
    }

    LIST_ADDEND(psLevels, psDataSet, LEVEL_DATASET);

    return TRUE;
  }
}

/* Rebuild the LEVEL_DATASET list from the resident document */
BOOL ManifestRegisterDataSets(void)
{
  if (!g_loaded)
  {
    Neuron::Fatal("ManifestRegisterDataSets: no manifest loaded");
    return FALSE;
  }

  const Neuron::Json* dataSets = g_document.Find("datasets");
  if (dataSets == nullptr || !dataSets->IsArray())
  {
    Neuron::Fatal("ManifestRegisterDataSets: manifest has no dataset list");
    return FALSE;
  }

  for (std::size_t i = 0; i < dataSets->Size(); i++)
  {
    if (!RegisterDataSet(dataSets->Item(i)))
      return FALSE;
  }

  return TRUE;
}

/* Read and parse GameData/datasets.json, then register the level datasets */
BOOL ManifestLoad(void)
{
  UBYTE* pBuffer;
  UDWORD size;

  if (!loadFile("datasets.json", &pBuffer, &size))
    return FALSE;

  auto parsed = Neuron::Json::Parse(std::string_view(reinterpret_cast<char*>(pBuffer), size));
  delete[] pBuffer;
  pBuffer = nullptr;

  if (!parsed.has_value())
  {
    Neuron::Fatal("datasets.json: parse error at line {} column {}: {}",
                  parsed.error().line, parsed.error().column, parsed.error().message);
    return FALSE;
  }

  g_document = std::move(*parsed);
  g_loaded = true;

  const Neuron::Json* units = g_document.Find("units");
  if (units == nullptr || !units->IsObject())
  {
    Neuron::Fatal("datasets.json: no unit table");
    return FALSE;
  }
  g_units.clear();
  for (const auto& [unitName, unit] : units->Members())
    g_units.emplace(unitName, &unit);

  return ManifestRegisterDataSets();
}

/* Bind the pages of a texture group, and optionally create them.
 *
 * A group is a list of page bindings; one may extend another, in which case
 * the base group's bindings come first, in its order, with the extending
 * group's replacing them in place. Binding order is therefore stable across
 * every group, which is what keeps a page in the same slot however the
 * player reached the level.
 *
 * A page may name two files - a "-hard" and a "-soft" variant - and the
 * translucency setting picks one. That test was the first thing the TEXPAGE
 * loader did; it lives here now, and being resolved at bind time is what
 * lets a set switch notice that the chosen file changed.
 */
BOOL ManifestApplyTextureSet(const STRING* _group, BOOL _create)
{
  std::vector<Neuron::TextureCache::PageBinding> bindings;

  if (!CollectGroup(_group, bindings, 0))
    return FALSE;

  if (!Neuron::TextureCache::SetBindings(bindings))
    return FALSE;

  if (_create && !Neuron::TextureCache::CreateBoundPages())
    return FALSE;

  return TRUE;
}

/* Free the resident manifest document */
void ManifestShutDown(void)
{
  g_units.clear();
  g_document = Neuron::Json();
  g_loaded = false;
}

/* Whether a unit with this name exists */
BOOL ManifestHasUnit(const STRING* _name)
{
  return FindUnit(_name) != nullptr ? TRUE : FALSE;
}

/* Load every resource a unit lists - the replacement for resLoad on a .wrf */
BOOL ManifestLoadUnit(const STRING* _name, SDWORD _blockID, UBYTE* _loadBuffer, SDWORD _bufferSize)
{
  const Neuron::Json* unit = FindUnit(_name);
  if (unit == nullptr || !unit->IsArray())
  {
    Neuron::Fatal("ManifestLoadUnit: unknown unit {}", _name);
    return FALSE;
  }

  resBeginBlock(_blockID, _loadBuffer, _bufferSize);

  for (std::size_t i = 0; i < unit->Size(); i++)
  {
    const Neuron::Json& entry = unit->Item(i);
    const Neuron::Json* dir = entry.Find("d");
    const Neuron::Json* type = entry.Find("t");
    const Neuron::Json* file = entry.Find("f");
    if (dir == nullptr || !dir->IsString() || type == nullptr || !type->IsString()
      || file == nullptr || !file->IsString())
    {
      Neuron::Fatal("ManifestLoadUnit: {} entry {} is malformed", _name, i);
      return FALSE;
    }

    /* A texture set is not a file, so it does not reach the resource
       system: it names a group in the texturePages table and binds that
       group's pages here, where the unit's TEXPAGE entries used to sit.
       Binding is what the level needs; creating the pages is what the old
       loader did in the same place, and "create": false opts out of it -
       the front end binds so the force editor can draw, but nothing on the
       title screen wants a page. */
    if (type->AsString() == "TEXSET")
    {
      const Neuron::Json* create = entry.Find("create");
      if (!ManifestApplyTextureSet(file->AsString().c_str(), create == nullptr || create->AsBool()))
        return FALSE;
      continue;
    }

    // resLoadFile takes mutable strings; give it bounded local copies
    STRING aType[RESTYPE_MAXCHAR];
    STRING aFile[FILE_MAXCHAR];
    if (type->AsString().size() >= RESTYPE_MAXCHAR || file->AsString().size() >= FILE_MAXCHAR)
    {
      Neuron::Fatal("ManifestLoadUnit: {} entry {} name too long", _name, i);
      return FALSE;
    }
    strcpy(aType, type->AsString().c_str());
    strcpy(aFile, file->AsString().c_str());

    resSetDirectory(dir->AsString().c_str());
    if (!resLoadFile(aType, aFile))
      return FALSE;
  }

  return TRUE;
}
