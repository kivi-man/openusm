#pragma once

namespace culling_params {
    // Expanded from 140.0f to match TERRAIN_STREAMING_DISTANCE (2500m)
    // This allows HD building geometry (region meshes, legos) to render at distance
    inline constexpr auto entity_traversal_distance = 2500.0f;
}

