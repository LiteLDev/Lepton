#include "feature/enchanting/Config.h"

#include "mod/Config.h"

LP_REGISTER_CONFIG("enchanting", lp::feature::enchanting::EnchantingConfig);

namespace lp::feature::enchanting {

EnchantingConfig const& cfg() { return config::get<EnchantingConfig>(); }

} // namespace lp::feature::enchanting
