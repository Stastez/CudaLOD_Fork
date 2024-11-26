cmake_minimum_required(VERSION 3.16)

project(CudaLOD LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

add_subdirectory(libs/brotli)

add_library(toojpeg libs/toojpeg/toojpeg.cpp libs/toojpeg/toojpeg.h)

find_package(GLEW REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(CUDAToolkit REQUIRED)
find_package(OpenVR REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(implot CONFIG REQUIRED)

find_path(LASZIP_DIR include/laszip/laszip_api.h REQUIRED)
find_library(LASZIP_LIB laszip REQUIRED)

set(CPP_SOURCES
		src/main.cpp
		src/main_simlod.h
		src/GLTimerQueries.cpp
		src/Framebuffer.cpp
		src/Renderer.cpp
		src/OpenVRHelper.cpp
		src/Texture.cpp
		src/OrbitControls.cpp

		include/OpenVRHelper.h
		include/unsuck.hpp
		include/unsuck_platform_specific.cpp
		include/Renderer.h
		include/Shader.h
		include/compute_basic.h
		include/Runtime.h
		include/OrbitControls.h
		include/Camera.h
		include/Box.h
		include/Framebuffer.h
		include/Texture.h
		include/GLBuffer.h
		include/Frustum.h
		include/GLTimerQueries.h
		include/Method.h
		include/TaskPool.h
		include/Plane.h

		modules/compute/LasLoaderSparse.cpp
		modules/compute/LasLoaderSparse.h
		modules/compute/ComputeLasLoader.cpp
		modules/compute/ComputeLasLoader.h
		modules/compute/PotreeData.h
		modules/compute/Resources.h
		modules/simlod/SimLOD.h
		modules/simlod/voxel_sampling_gentree/voxel_sampling_gentree.h
		modules/simlod/sampling_cuda/sampling_cuda.h
		modules/simlod/sampling_cuda_nonprogressive/sampling_cuda_nonprogressive.h
		modules/simlod/sampling_cuda_nonprogressive/OctreeWriter.h
)

set(CUDA_SOURCES
		modules/simlod/sampling_cuda_nonprogressive/lib.cu
		modules/simlod/sampling_cuda_nonprogressive/lib.h.cu
		modules/simlod/sampling_cuda_nonprogressive/kernel.cu
		modules/simlod/sampling_cuda_nonprogressive/render.cu
		modules/simlod/sampling_cuda_nonprogressive/methods_common.h.cu
		modules/simlod/sampling_cuda_nonprogressive/voxelize_sampleselect_first_blockwise.cu
		modules/simlod/sampling_cuda_nonprogressive/voxelize_sampleselect_random_blockwise_globalmem.cu
		modules/simlod/sampling_cuda_nonprogressive/voxelize_singlecell_blockwise.cu
		modules/simlod/sampling_cuda_nonprogressive/voxelize_neighborhood_blockwise.cu

		modules/simlod/sampling_cuda_nonprogressive/common.h
		modules/simlod/sampling_cuda_nonprogressive/helper_math.h
)

add_executable(CudaLOD ${CPP_SOURCES})

set(INCLUDE_DIRS
		include
		modules
		libs/toojpeg
		libs/glm
		${LASZIP_DIR}
		modules/simlod/sampling_cuda_nonprogressive
)

target_include_directories(CudaLOD PRIVATE ${INCLUDE_DIRS})

set(LIBS
		toojpeg
		${LASZIP_LIB}
		GLEW::GLEW
		imgui::imgui
		implot::implot
		nlohmann_json::nlohmann_json
		OpenVR::OpenVR
		CUDA::cublas
		CUDA::cudart
		CUDA::nvrtc
		CUDA::curand
		brotlicommon
		brotlidec
		brotlienc
)

target_link_libraries(CudaLOD ${LIBS})