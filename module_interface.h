#ifndef MODULE_INTERFACE_H
#define MODULE_INTERFACE_H

#include <stdint.h>
#include <time.h>

/* interface version constants */
#define MODULE_INTERFACE_VERSION_1 1U
#define MODULE_INTERFACE_VERSION_CURRENT MODULE_INTERFACE_VERSION_1

/* module init args structure version */
#define MODULE_INIT_ARGS_VERSION_1 1U
#define MODULE_INIT_ARGS_VERSION_CURRENT MODULE_INIT_ARGS_VERSION_1

/* module init args structure */
typedef struct {
    uint32_t version;
    void (*log)(int level, const char *fmt, ...);
    int (*get_time)(struct timespec *ts);
    void *user_data;
} module_init_args_t;

/* get module interface version
 * must be implemented by every module
 * returns interface version number
 */
uint32_t module_get_interface_version(void);

/* module initialization function
 * must be implemented by every module
 * called after module is loaded
 * @param init_args initialization arguments or NULL
 * @return 0 on success, negative value on error
 */
int module_init(const void *init_args);

/* module finalization function
 * must be implemented by every module
 * called before module is unloaded
 */
void module_fini(void);

#endif /* MODULE_INTERFACE_H */

