add_executable(test_frame_codec test_frame_codec.cpp)
target_link_libraries(test_frame_codec PRIVATE massage_core)

add_test(NAME frame_codec COMMAND test_frame_codec)
