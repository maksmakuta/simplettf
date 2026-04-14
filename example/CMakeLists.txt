add_executable(ettf
    main.cpp
)

target_include_directories(ettf PRIVATE ../include)
target_link_libraries(ettf PRIVATE simplettf)