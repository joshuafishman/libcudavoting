cmake_minimum_required(VERSION 2.4.6)
project(tensorvoting)

if(CMAKE_COMPILER_IS_GNUCXX)
	execute_process(COMMAND ${CMAKE_C_COMPILER} -dumpversion OUTPUT_VARIABLE GCC_VERSION)
	if (GCC_VERSION VERSION_GREATER 4.4 OR GCC_VERSION VERSION_EQUAL 4.4)
		add_definitions(-std=gnu++0x)
	else (GCC_VERSION VERSION_GREATER 4.4 OR GCC_VERSION VERSION_EQUAL 4.4)
		message(SEND_ERROR "You need GCC version 4.4 or greater to compile this library")
	endif (GCC_VERSION VERSION_GREATER 4.4 OR GCC_VERSION VERSION_EQUAL 4.4)
else(CMAKE_COMPILER_IS_GNUCXX)
	message(SEND_ERROR "You need partial C++0x support for N2351, N2672, N1984. Currently this build toolchain supports only GCC >= 4.4. If your compiler supports these specifications, please send us a patch.")
endif(CMAKE_COMPILER_IS_GNUCXX)

find_package(catkin REQUIRED COMPONENTS 
  roscpp
  libpointmatcher_ros
  sensor_msgs
)


# add NABO
find_path(NABO_INCLUDE_DIRS nabo/nabo.h
	${ethzasl_icp_mapping_STACK_PATH}/libnabo/include
)
find_library(NABO_LIBRARIES nabo
	${ethzasl_icp_mapping_STACK_PATH}/libnabo/build
)
message("NABO_INCLUDE_DIRS is " ${NABO_INCLUDE_DIRS} )

# boost
find_package(Boost COMPONENTS thread filesystem system REQUIRED)

# LINK_DIRECTORIES( ${LINK_DIRECTORIES} /usr/lib)

# look for yaml-cpp library
find_path(yaml-cpp_INCLUDE_DIRS yaml-cpp/yaml.h
	/usr/local/include
	/usr/include
)
find_library(yaml-cpp_LIBRARIES yaml-cpp PATHS
	/usr/local/lib
	/usr/lib
NO_DEFAULT_PATH)
if (yaml-cpp_INCLUDE_DIRS AND yaml-cpp_LIBRARIES)
	include_directories(${yaml-cpp_INCLUDE_DIRS})
	set(EXTRA_LIBS ${EXTRA_LIBS} ${yaml-cpp_LIBRARIES})
	add_definitions(-DHAVE_YAML_CPP)
endif (yaml-cpp_INCLUDE_DIRS AND yaml-cpp_LIBRARIES)

# look for Eigen library
find_path(eigen_INCLUDE_DIRS Eigen/Eigen
	/usr/local/include
	/usr/include/eigen3
    /usr/include
)

if (eigen_INCLUDE_DIRS)
	include_directories(${eigen_INCLUDE_DIRS})
	add_definitions(-DHAVE_EIGEN)
else(eigen_INCLUDE_DIRS)
        message("Eigen Not Found!")
endif (eigen_INCLUDE_DIRS)

find_package(CUDA QUIET)
if(CUDA_FOUND)
#SET(CUDA_NVCC_FLAGS "-arch=sm_30" "-O3" "-use_fast_math")
    SET(CUDA_NVCC_FLAGS "-arch=sm_30" "-O3" "-use_fast_math")
    include_directories( ${CUDA_INCLUDE_DIRS} /usr/local/cuda-10.1/samples/common/inc )
    message("CUDA include directories: ###" ${CUDA_INCLUDE_DIRS} " ###" ${CUDA_INCLUDE_DIRS}/samples/common/inc/  )
    add_definitions(-DUSE_CUDA)
else(CUDA_FOUND)
    message("CUDA is not installed on this system.")
endif()
# Build the kernel and the kernel-aware host code
CUDA_ADD_LIBRARY(CudaVoting src/CudaVoting.cu)

include_directories(include
					${NABO_INCLUDE_DIRS}
					${Boost_INCLUDE_DIRS}
					${catkin_INCLUDE_DIRS})

catkin_package(
   INCLUDE_DIRS include
   LIBRARIES ${PROJECT_NAME}
   CATKIN_DEPENDS   roscpp
   					libpointmatcher_ros
   DEPENDS  Boost 
			NABO)

cuda_add_library(${PROJECT_NAME} src/tensor_voting.cpp 
							     src/CudaVoting.cu)

# sparse voting demo
add_executable(demo_sparse_gpu src/demo_sparse_gpu.cpp src/tensor_voting.cpp)
target_link_libraries(demo_sparse_gpu ${libpointmatcher_ros_LIBRARIES} CudaVoting ${NABO_LIBRARIES} ${EXTRA_LIBS} cuda rt ${Boost_LIBRARIES})

# sparse + dense stick voting demo (use unified GPU implementation)
add_executable(demo_dense_gpu src/demo_dense_gpu.cpp src/tensor_voting.cpp)
target_link_libraries(demo_dense_gpu ${libpointmatcher_ros_LIBRARIES} CudaVoting ${NABO_LIBRARIES} ${EXTRA_LIBS} cuda rt ${Boost_LIBRARIES})
