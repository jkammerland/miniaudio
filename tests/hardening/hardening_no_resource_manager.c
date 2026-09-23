#define MA_NO_RESOURCE_MANAGER
#define MINIAUDIO_IMPLEMENTATION
#include "../../miniaudio.h"

int main(void)
{
    return ma_version_string() == NULL;
}
