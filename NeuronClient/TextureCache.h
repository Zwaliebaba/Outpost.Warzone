#pragma once

#include <span>
#include <string>

namespace Neuron
{

/// The texture pages the renderer draws from, and the decoded pixels behind
/// them.
///
/// A page is named by its id - "page-17", the name a .pie TEXTURE directive
/// reduces to - and bound to the file its pixels come from. Which file that
/// is depends on the level's tileset and on the additive setting, so the
/// binding is data the manifest supplies rather than something this module
/// works out; see GameData/datasets.json's texturePages table.
///
/// The pixels are cached by file for the life of the process: a page whose
/// binding changes and later changes back is not decoded twice, and a
/// campaign switch no longer re-reads what it already had. That cache is
/// also the device-reset backing store, because _TEX_PAGE[i].tex.bmp points
/// into it - which is why nothing here frees a sprite before Shutdown.
///
/// Was the TEXPAGE resource type, whose entries listed the same files once
/// per tileset and whose loader created every page whether a model wanted
/// it or not.
class TextureCache
{
public:
  /// One page's resolved binding. A public aggregate, so plain fields.
  struct PageBinding
  {
    std::string pageId; // "page-17"
    std::string file; // "texpages\\Page-17-Droid Weapons.dds"
  };

  /// Replaces the page bindings. A page that already has a slot keeps it:
  /// if its file changed the slot is refilled and re-uploaded in place, so
  /// the texpage indices already baked into loaded models stay right. This
  /// is what a camchange dataset does when it swaps the tileset.
  static bool SetBindings(std::span<const PageBinding> _bindings);

  /// Creates every bound page that has no slot yet, in binding order.
  /// Datasets that declare a tileset call this where their manifest used to
  /// list the pages, which keeps page creation ahead of the terrain tiles
  /// that are appended after it. The front end declares no tileset and so
  /// creates nothing until something asks.
  static bool CreateBoundPages();

  /// The slot a page id draws from, creating it on first use. -1 if the id
  /// is not bound or its file cannot be read.
  [[nodiscard]] static int PageIndex(const char* _pageId);

  /// Frees the decoded pixels. The pages themselves belong to Tex.cpp.
  static void Shutdown();
};

} // namespace Neuron
