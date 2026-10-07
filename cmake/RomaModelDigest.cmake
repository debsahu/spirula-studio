# cmake -DROOT=<src> -DOUT=<file.cpp> -P RomaModelDigest.cmake
# Writes roma::modelSourceDigest(): a SHA-256 over every source that decides
# what the matcher computes. The warp cache keys on it, so a changed kernel
# can never be served an old warp.
file(GLOB_RECURSE files RELATIVE ${ROOT}
     ${ROOT}/nn/*.cpp ${ROOT}/nn/*.h ${ROOT}/nn/*.slang
     ${ROOT}/roma/model/*.cpp ${ROOT}/roma/model/*.h
     ${ROOT}/roma/shaders/* ${ROOT}/roma/Roma.h ${ROOT}/roma/Common.h)
list(FILTER files EXCLUDE REGEX "/tests/")
list(SORT files)
set(blob "")
foreach(f ${files})
    file(SHA256 ${ROOT}/${f} h)
    string(APPEND blob "${f}:${h}\n")
endforeach()
string(SHA256 digest "${blob}")
file(WRITE ${OUT} "namespace roma {\nconst char* modelSourceDigest() { return \"${digest}\"; }\n}  // namespace roma\n")
