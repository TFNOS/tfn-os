#pragma once

// THE ABI ALIASES: a function of the public API with a new name keeps its first name for the installed app packages.
// The first name stays in the ABI (main/tfn_abi.json: no name goes out inside one ABI version), and here it becomes a
// second name of the same function. Write TFN_ABI_ALIAS(old, new) after the function, in its own file (GCC needs the
// alias and the function in one file). tests/style/check_api.py checks that both names are ABI names. A host build
// (the simulator, the tests) loads no app packages: no alias there.
#if defined(ESP_PLATFORM)
#define TFN_ABI_ALIAS(old, name) extern __typeof__(name) old __attribute__((alias(#name)));
#else
#define TFN_ABI_ALIAS(old, name)
#endif
