#include "feature/inventory/Messages.h"

#include "net/Net.h"

using namespace lp::feature::inventory;

LP_REGISTER_MESSAGE(SortRequest);
LP_REGISTER_MESSAGE(TransferRequest);
LP_REGISTER_MESSAGE(QuickMoveRequest);
LP_REGISTER_MESSAGE(ChangeHotbarRequest);
LP_REGISTER_MESSAGE(SlotInteractRequest);
LP_REGISTER_MESSAGE(ScrollBundleRequest);
LP_REGISTER_MESSAGE(ShareItemRequest);
LP_REGISTER_MESSAGE(SharedItemNotice);
