# Called at POST_BUILD: copy foo.hex -> foo_YYYYMMDD_HHMMSS.hex
if(NOT DEFINED HEX_DIR OR NOT DEFINED HEX_BASE)
  message(FATAL_ERROR "stamp_hex.cmake needs -DHEX_DIR= and -DHEX_BASE=")
endif()

string(TIMESTAMP TS "%Y%m%d_%H%M%S")
set(SRC "${HEX_DIR}/${HEX_BASE}.hex")
set(DST "${HEX_DIR}/${HEX_BASE}_${TS}.hex")

if(NOT EXISTS "${SRC}")
  message(FATAL_ERROR "Missing hex: ${SRC}")
endif()

file(COPY_FILE "${SRC}" "${DST}")
message(STATUS "Stamped hex: ${DST}")
