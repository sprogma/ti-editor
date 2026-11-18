#include <types.h>
#include <uv.h>

Time get_time()
{
    return uv_hrtime();
}

Time timeout_from_now(Time ns)
{
    return get_time() + ns;
}

void init_types() 
{
}
