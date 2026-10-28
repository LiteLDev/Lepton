#include "feature/inventory/Config.h"

#include "mod/Config.h"

LP_REGISTER_CONFIG("inventory", lp::feature::inventory::InventoryConfig);

namespace lp::feature::inventory {
InventoryConfig const& cfg() { return config::get<InventoryConfig>(); }
} // namespace lp::feature::inventory
