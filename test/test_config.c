#include <assert.h>
#include "../src/config.h"
#include "../src/util.h"

int main(void)
{
    config_t config;
    config_load(&config, "config-test.json");

    assert(config.port == 8000);
    assert(config.daemon == false);
    assert(config.worker == 4);
    assert(config.timeout == 30);

    return 0;
}
