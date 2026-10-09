#include <stddef.h>

#include "svs.h"
#include "svs_compute.h"

_Static_assert(sizeof(svsc_handle) == 8, "Handle must be fixed-width");
_Static_assert(sizeof(((svsc_tensor*)0)->byte_count) == 8, "Byte count must be fixed-width");
_Static_assert(offsetof(svsc_api, size) == 0, "Size prefix must be first");
_Static_assert(offsetof(svsc_api, abi_version) == 4, "Version prefix must be stable");

int main(void)
{
	svsc_tensor tensor = {0};
	tensor.size = sizeof(tensor);
	tensor.dtype = SVSC_FLOAT32;
	return tensor.rank == 0 && SVSC_ABI_VERSION == 0x00010000u ? 0 : 1;
}
