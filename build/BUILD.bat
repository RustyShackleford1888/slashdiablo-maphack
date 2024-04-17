cmake -G"Visual Studio 15 2017" -DBUILD_SHARED_LIBS=TRUE -DCMAKE_WINDOWS_EXPORT_ALL_SYMBOLS=TRUE ..
TIMEOUT /T 3
cmake --build . --config Release