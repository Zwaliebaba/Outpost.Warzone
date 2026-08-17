#include "pch.h"
/***************************************************************************/
/*
 * TextureCache.cpp
 *
 * Page bindings and the decoded pixels behind them. See TextureCache.h for
 * what this replaced and why the pixels outlive the pages.
 */
/***************************************************************************/

#include <string>
#include <unordered_map>
#include <vector>

#include "Frame.h"
#include "RenderTypes.h"
#include "Dds.h"
#include "Tex.h"
#include "TextureCache.h"

namespace Neuron
{

namespace
{

/* The type and colour-key flag every texture page has carried since the
 * TEXPAGE loader set them. Neither is read by anything that draws - the
 * type reaches only the .pie writer and the flag nothing at all - but they
 * are what the pages hold today, so they are what the pages keep holding.
 */
constexpr int PageType = 1;
constexpr iBool PageColourKeyed = FALSE;

struct Page
{
  std::string pageId;
  std::string file; // mutable: DdsLoad takes char*, and this is its buffer
  int slot = -1; // index into _TEX_PAGE, -1 until the page is created
};

/* In binding order, which is the order pages are created in and therefore
 * the order they take slots in.
 */
std::vector<Page> g_pages;

/* Decoded pixels by file. Owns every bmp it holds; _TEX_PAGE aliases them. */
std::unordered_map<std::string, iSprite> g_sprites;

/***************************************************************************/

/* The cache key for a path. The manifests spell the same texture with
 * different capitalisation in different groups - cam3change's page-7 and
 * vidmemc's are one file written two ways - and NTFS gives them the same
 * bytes, so keying on the exact spelling would decode that file twice and
 * make a rebind out of a change of case.
 */
std::string CacheKey(std::string_view _file)
{
  std::string key(_file);
  for (char& c : key)
    c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
  return key;
}

/***************************************************************************/

/* Decodes a file if it has not been decoded before. Returns null if it
 * cannot be read, which costs the page rather than the level.
 */
iSprite* EnsureSprite(std::string& _file)
{
  const std::string key = CacheKey(_file);

  const auto found = g_sprites.find(key);
  if (found != g_sprites.end())
    return &found->second;

  iSprite sprite = {};
  if (!Neuron::DdsLoad(_file.data(), &sprite))
  {
    Neuron::DebugTrace("TextureCache: could not load {}\n", _file);
    return nullptr;
  }

  return &g_sprites.emplace(key, sprite).first->second;
}

/***************************************************************************/

Page* FindPage(const char* _pageId)
{
  for (Page& page : g_pages)
  {
    if (stricmp(page.pageId.c_str(), _pageId) == 0)
      return &page;
  }

  return nullptr;
}

/***************************************************************************/

bool Binds(const std::vector<Page>& _pages, const std::string& _pageId)
{
  for (const Page& page : _pages)
  {
    if (stricmp(page.pageId.c_str(), _pageId.c_str()) == 0)
      return true;
  }

  return false;
}

/***************************************************************************/

/* Points an existing slot at a different file's pixels. Tex.cpp owns the
 * page array, so the slot itself is repointed there; this only decides
 * which pixels it should hold.
 */
bool RefillPage(Page& _page)
{
  iSprite* sprite = EnsureSprite(_page.file);
  if (sprite == nullptr)
    return false;

  return pie_RefillTexPage(_page.slot, sprite) != FALSE;
}

/***************************************************************************/

bool CreatePage(Page& _page)
{
  iSprite* sprite = EnsureSprite(_page.file);
  if (sprite == nullptr)
    return false;

  /* bResource TRUE: the bitmap belongs to this cache, so pie_TexShutDown
   * must not free it - the same contract the resource-owned pages had.
   */
  _page.slot = pie_AddBMPtoTexPages(sprite, _page.pageId.data(), PageType, PageColourKeyed, TRUE);

  return _page.slot >= 0;
}

} // namespace

/***************************************************************************/

bool TextureCache::SetBindings(std::span<const PageBinding> _bindings)
{
  std::vector<Page> next;
  next.reserve(_bindings.size());
  bool ok = true;

  for (const PageBinding& binding : _bindings)
  {
    Page page;
    page.pageId = binding.pageId;
    page.file = binding.file;

    /* carry over the slot of a page that already exists, and refill it if
     * this set binds it to a different file
     */
    if (const Page* existing = FindPage(binding.pageId.c_str()); existing != nullptr && existing->slot >= 0)
    {
      const bool changed = CacheKey(existing->file) != CacheKey(page.file);
      page.slot = existing->slot;

      if (changed && !RefillPage(page))
      {
        Neuron::DebugTrace("TextureCache: could not rebind {} to {}\n", page.pageId, page.file);
        ok = false;
      }
    }

    next.push_back(std::move(page));
  }

  /* A page the new bindings drop keeps its slot and its pixels: nothing
   * reclaims a slot, and a model loaded under the old set may still be
   * drawing it.
   */
  for (const Page& page : g_pages)
  {
    if (page.slot >= 0 && Binds(next, page.pageId) == false)
      next.push_back(page);
  }

  g_pages = std::move(next);

  return ok;
}

/***************************************************************************/

bool TextureCache::CreateBoundPages()
{
  bool ok = true;

  for (Page& page : g_pages)
  {
    if (page.slot < 0 && !CreatePage(page))
    {
      Neuron::DebugTrace("TextureCache: could not create {} from {}\n", page.pageId, page.file);
      ok = false;
    }
  }

  return ok;
}

/***************************************************************************/

int TextureCache::PageIndex(const char* _pageId)
{
  Page* page = FindPage(_pageId);

  if (page == nullptr)
  {
    Neuron::DebugTrace("TextureCache: no page bound for {}\n", _pageId);
    return -1;
  }

  if (page->slot < 0 && !CreatePage(*page))
    return -1;

  return page->slot;
}

/***************************************************************************/

void TextureCache::Shutdown()
{
  for (auto& [file, sprite] : g_sprites)
  {
    delete[] sprite.bmp;
    sprite.bmp = nullptr;
  }

  g_sprites.clear();
  g_pages.clear();
}

} // namespace Neuron
