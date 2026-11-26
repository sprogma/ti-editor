#ifndef NETWORK_H
#define NETWORK_H

#include <api.h>
#include <ds.h>
#include <mp.h>

i32 init_network_subsystem(CoreConfig *cfg);

// TODO: add hook via callback argument
//       [or add check for waiting tasks inside deserialize_operation]
void request_operation(OperationId op_id);

#endif // NETWORK_H
