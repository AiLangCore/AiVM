cmake_minimum_required(VERSION 3.16)
project(aivm_native_bridge C)

# Transitional wrapper: keep canonical C VM sources under AiVM.C until full cutover.
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../../AiVM.C ${CMAKE_CURRENT_BINARY_DIR}/aivm_legacy)
