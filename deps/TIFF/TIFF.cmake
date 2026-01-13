find_package(OpenGL QUIET REQUIRED)

bambustudio_add_cmake_project(TIFF
    URL https://gitlab.com/libtiff/libtiff/-/archive/v4.3.0/libtiff-v4.3.0.zip
    URL_HASH SHA256=455abecf8fba9754b80f8eff01c3ef5b24a3872ffce58337a59cba38029f0eca
    DEPENDS ${ZLIB_PKG} ${PNG_PKG} ${JPEG_PKG}
    CMAKE_ARGS
        -Dlzma:BOOL=OFF
        -Dwebp:BOOL=OFF
        -Djbig:BOOL=OFF
        -Dzstd:BOOL=OFF
        -Dpixarlog:BOOL=OFF
        -Dtiff-tools:BOOL=OFF  # GLUT OFF
        -Dtiff-tests:BOOL=OFF  # GLUT OFF
        -DCMAKE_DISABLE_FIND_PACKAGE_GLUT=ON  # GLUT OFF <--- w 4.3.0 to bedzie to chyba
        -DCMAKE_DISABLE_FIND_PACKAGE_OpenGL=ON # <--- też nie potrzebne
)
