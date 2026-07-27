#include <bielik/bk_app.h>

#define BK_STR_(x) #x
#define BK_STR(x) BK_STR_(x)

const char *bk_version_string(void) {
    return BK_STR(BK_VERSION_MAJOR) "." BK_STR(BK_VERSION_MINOR) "." BK_STR(BK_VERSION_PATCH);
}
