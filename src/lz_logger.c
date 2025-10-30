#include "lz_logger.h"

FFI_PLUGIN_EXPORT void lz_logger(int loglevel, const char *tag,
                                 const char *file_name, int line,
                                 const char *message) {
  (void)loglevel;
  (void)tag;
  (void)file_name;
  (void)line;
  (void)message;
  // TODO: implement shared logging core. Placeholder keeps the symbol available
  // for FFI bindings.
}
