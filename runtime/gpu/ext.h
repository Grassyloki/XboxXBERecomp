#pragma once
// A game's addition to the renderer: its shader hooks (the Vulkan renderer ignores the rest of what the Direct3D 12
// renderer, now gone, lent an extension). Nothing registers one yet.
#include <memory>

#include "gpu/shaders.h"

namespace gpu {

class Extension {
public:
    virtual ~Extension() = default;
    virtual const ShaderHooks* Shaders() const { return nullptr; }
};

inline std::unique_ptr<Extension> g_extension;

// before the backend is made
inline void SetExtension(std::unique_ptr<Extension> e) {
    g_extension = std::move(e);
    g_shader_hooks = g_extension ? g_extension->Shaders() : nullptr;
}

}  // namespace gpu
