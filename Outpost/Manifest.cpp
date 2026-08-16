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
 */

#include <string>
#include <unordered_map>

#include "Frame.h"
#include "FrameResource.h"
#include "ListMacs.h"
#include "Json.h"
#include "Levels.h"
#include "Manifest.h"

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
