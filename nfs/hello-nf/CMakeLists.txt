add_executable(hello-nf src/main.cpp)
target_link_libraries(hello-nf PRIVATE sbi_core)
