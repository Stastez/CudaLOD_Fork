
#pragma once

#include <string>
#include <format>

#include "glm/common.hpp"
#include "glm/matrix.hpp"
#include <glm/gtx/transform.hpp>
#include <execution>
#include <algorithm>
#include "brotli/encode.h"

#include "toojpeg.h"

#include "unsuck.hpp"
#include "utils.h"
#include "Box.h"

using namespace std;
using glm::vec3;

// RGBA encode(RGBA decoded_parent, RGBA decoded_child){

// }

// RGBA decode(RGBA encoded_child, RGBA decoded_parent){

// }

struct OctreeWriter{

	struct VoxelBuffer{
		shared_ptr<Buffer> positions = nullptr;
		shared_ptr<Buffer> colors = nullptr;
	};

	struct Point{
		float x;
		float y;
		float z;
		unsigned int color;
	};

	struct RGBA{
		int r;
		int g;
		int b;
		int a;
	};

	struct CuNode{
		int pointOffset;
		int numPoints;
		Point* points;
		
		uint32_t dbg = 0;
		int numAdded;
		int level;
		int voxelIndex;
		vec3 min;
		vec3 max;
		float cubeSize;
		CuNode* children[8] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};

		int numVoxels = 0;
		Point* voxels = nullptr;

		bool visible = true;

		void traverse(string name, std::function<void(CuNode*, string)> callback){

			callback(this, name);

			for(int i = 0; i < 8; i++){
				if(this->children[i] == nullptr) continue;

				this->children[i]->traverse(name + std::to_string(i), callback);
			}

		}
	};

	string path = "";
	Buffer* buffer = nullptr;
	int numNodes = 0;
	uint64_t offset_buffer = 0;
	uint64_t offset_nodes = 0;
	Box box;

	struct HNode{
		CuNode* cunode = nullptr;
		HNode* children[8];
		HNode* parent = nullptr;
		int childNumVoxels[8] = {0, 0, 0, 0, 0, 0, 0, 0};
		int voxelsPerOctant[8] = {0, 0, 0, 0, 0, 0, 0, 0};
		vector<RGBA> diffColors;
		shared_ptr<VoxelBuffer> voxelBuffer = nullptr;

		string name = "";

		void traverse(string name, std::function<void(HNode*, string)> callback){

			callback(this, name);

			for(int i = 0; i < 8; i++){
				if(this->children[i] == nullptr) continue;

				this->children[i]->traverse(name + std::to_string(i), callback);
			}

		}

		void traverse(std::function<void(HNode*)> callback){

			callback(this);

			for(int i = 0; i < 8; i++){
				if(this->children[i] == nullptr) continue;

				this->children[i]->traverse(callback);
			}

		}
	};

	OctreeWriter(string path, Box box, Buffer* buffer, int numNodes, uint64_t offset_buffer, uint64_t offset_nodes){
		this->path = path;
		this->buffer = buffer;
		this->numNodes = numNodes;
		this->offset_buffer = offset_buffer;
		this->offset_nodes = offset_nodes;
		this->box = box;
	}

	shared_ptr<Buffer> compress(shared_ptr<Buffer> buffer) {

		shared_ptr<Buffer> out = nullptr;
		{

			int quality = 6;
			int lgwin = BROTLI_DEFAULT_WINDOW;
			auto mode = BROTLI_DEFAULT_MODE;
			uint8_t* input_buffer = buffer->data_u8;
			size_t input_size = buffer->size;

			size_t encoded_size = input_size * 1.5 + 1'000;
			shared_ptr<Buffer> outputBuffer = make_shared<Buffer>(encoded_size);
			uint8_t* encoded_buffer = outputBuffer->data_u8;

			BROTLI_BOOL success = BROTLI_FALSE;

			for (int i = 0; i < 5; i++) {
				success = BrotliEncoderCompress(quality, lgwin, mode, input_size, input_buffer, &encoded_size, encoded_buffer);

				if (success == BROTLI_TRUE) {
					break;
				} else {
					encoded_size = (encoded_size + 1024) * 1.5;
					outputBuffer = make_shared<Buffer>(encoded_size);
					encoded_buffer = outputBuffer->data_u8;

					cout << std::format("reserved encoded_buffer size was too small. Trying again with size {}." , formatNumber(encoded_size)) << endl;
				}
			}

			if (success == BROTLI_FALSE) {
				stringstream ss;
				ss << "failed to compress. aborting conversion." ;
				cout << ss.str() << endl;

				exit(123);
			}

			out = make_shared<Buffer>(encoded_size);
			memcpy(out->data, encoded_buffer, encoded_size);
			
		}

		return out;
	}

	shared_ptr<Buffer> toPointBuffer(HNode* node){

		auto cunode = node->cunode;

		auto buffer = make_shared<Buffer>(node->cunode->numPoints * 16);

		for(int i = 0; i < cunode->numPoints; i++){
			Point point = cunode->points[i];

			buffer->set<float>(point.x, 16 * i + 0);
			buffer->set<float>(point.y, 16 * i + 4);
			buffer->set<float>(point.z, 16 * i + 8);
			buffer->set<uint32_t>(point.color, 16 * i + 12);
		}

		return buffer;
	}

	shared_ptr<VoxelBuffer> toVoxelBuffer(HNode* node){

		auto cunode = node->cunode;

		shared_ptr<VoxelBuffer> result = make_shared<VoxelBuffer>();
		result->colors = make_shared<Buffer>(4 * node->cunode->numVoxels);

		Box cube(cunode->min, cunode->max);
		vec3 size = cube.size();

		{
			int gridSize = 128;
			float fGridSize = gridSize;

			for(int i = 0; i < cunode->numVoxels; i++){
				Point point = cunode->voxels[i];
				float fx = 128.0 * (point.x - cunode->min.x) / size.x;
				float fy = 128.0 * (point.y - cunode->min.y) / size.y;
				float fz = 128.0 * (point.z - cunode->min.z) / size.z;

				int cx = fx > 64.0f ? 1 : 0;
				int cy = fy > 64.0f ? 1 : 0;
				int cz = fz > 64.0f ? 1 : 0;

				int childIndex = (cx << 2) | (cy << 1) | (cz << 0);

				node->voxelsPerOctant[childIndex]++;
			}
		}

		if(node->name == "r"){
			// encode parent coordinates directly
			auto buffer = make_shared<Buffer>(node->cunode->numVoxels * 3);

			for(int i = 0; i < node->cunode->numVoxels; i++){
				Point voxel = node->cunode->voxels[i];

				float fx = 128.0 * (voxel.x - cunode->min.x) / size.x;
				float fy = 128.0 * (voxel.y - cunode->min.y) / size.y;
				float fz = 128.0 * (voxel.z - cunode->min.z) / size.z;

				uint8_t cx = clamp(fx, 0.0f, 127.0f);
				uint8_t cy = clamp(fy, 0.0f, 127.0f);
				uint8_t cz = clamp(fz, 0.0f, 127.0f);

				buffer->set<uint8_t>(cx, 3 * i + 0);
				buffer->set<uint8_t>(cy, 3 * i + 1);
				buffer->set<uint8_t>(cz, 3 * i + 2);
			}

			result->positions = buffer;
			// return buffer;
		}else if(node->cunode->numVoxels > 0){

			// encode child coordinates relative to parent voxels


			// first, create childmasks in voxel grid
			int gridSize = 64;
			float fGridSize = gridSize;
			vector<uint8_t> childmasks(gridSize * gridSize * gridSize, 0);

			for(int i = 0; i < cunode->numVoxels; i++){
				Point point = cunode->voxels[i];
				float fx = 128.0 * (point.x - cunode->min.x) / size.x;
				float fy = 128.0 * (point.y - cunode->min.y) / size.y;
				float fz = 128.0 * (point.z - cunode->min.z) / size.z;

				bool outsideX = (fx < 0.0f || fx >= 128.0f);
				bool outsideY = (fy < 0.0f || fy >= 128.0f);
				bool outsideZ = (fz < 0.0f || fz >= 128.0f);

				if(outsideX || outsideY || outsideZ){
					cout << std::format("out-of-bounds({}): pxyz: [{}, {}, {}], fxyz: [{}, {}, {}]", 
						node->name,
						point.x, point.y, point.z,
						fx, fy, fz) << endl;
				}

				// assert(fx >= 0.0f && fx < 128.0f);
				// assert(fy >= 0.0f && fy < 128.0f);
				// assert(fz >= 0.0f && fz < 128.0f);

				int ix_h = clamp(fx / 2.0f, 0.0f, fGridSize - 1.0f);
				int iz_h = clamp(fz / 2.0f, 0.0f, fGridSize - 1.0f);
				int iy_h = clamp(fy / 2.0f, 0.0f, fGridSize - 1.0f);

				int cx = clamp(fx - 2.0 * float(ix_h), 0.0, 1.0);
				int cy = clamp(fy - 2.0 * float(iy_h), 0.0, 1.0);
				int cz = clamp(fz - 2.0 * float(iz_h), 0.0, 1.0);

				int childIndex = (cx << 2) | (cy << 1) | (cz << 0);

				int voxelIndex = ix_h + iy_h * gridSize + iz_h * gridSize * gridSize;

				assert(voxelIndex < childmasks.size());

				int oldMask = childmasks[voxelIndex];

				childmasks[voxelIndex] = childmasks[voxelIndex] | (1 << childIndex);
			}

			// then iterate through all cells and check computed child masks
			int numParentVoxels = 0;
			int numChildVoxels = 0;
			vector<uint8_t> childmasks_list;
			for(int i = 0; i < childmasks.size(); i++){

				int mortonCode = i;
				int x = 0;
				int y = 0;
				int z = 0;

				for(int mi = 0; mi < 6; mi++){
					x = x | (((mortonCode >> (3 * mi + 2)) & 1) << mi);
					y = y | (((mortonCode >> (3 * mi + 1)) & 1) << mi);
					z = z | (((mortonCode >> (3 * mi + 0)) & 1) << mi);
				}

				int voxelIndex = x + y * gridSize + z * gridSize * gridSize;

				if(childmasks[voxelIndex] > 0){
					numParentVoxels++;

					//assert(numParentVoxels <= node->parent->cunode->numVoxels);

					uint8_t childmask = childmasks[voxelIndex];
					// fout.write((const char*)(&childmask), 1);
					childmasks_list.push_back(childmask);

					// DEBUG
					for(int	childIndex = 0; childIndex < 8; childIndex++){
						int bit = (childmask >> childIndex) & 1;
						if(bit == 1){
							numChildVoxels++;

							int nodeChildIndex = std::stoi(node->name.substr(node->name.size() - 1, 1));
							node->parent->childNumVoxels[nodeChildIndex]++;
						}
					}

					// int nodeChildIndex = std::stoi(node->name.substr(node->name.size() - 1, 1));
					// node->parent->voxelsPerOctant[nodeChildIndex]++;

				}
			}

			//assert(node->parent->cunode->numVoxels == numParentVoxels);

			auto buffer = make_shared<Buffer>(childmasks_list.size());
			memcpy(buffer->data, childmasks_list.data(), childmasks_list.size());

			result->positions = buffer;
		}
		// else{
		// 	return make_shared<Buffer>(0);
		// }

		return result;
	}

	shared_ptr<Buffer> toColorBuffer(HNode* node){

		auto cunode = node->cunode;

		auto buffer = make_shared<Buffer>(node->cunode->numVoxels * 3);

		for(int i = 0; i < node->cunode->numVoxels; i++){
			Point voxel = node->cunode->voxels[i];
			uint8_t* rgba = (uint8_t*)&voxel.color;

			buffer->set<uint8_t>(rgba[0], 3 * i + 0);
			buffer->set<uint8_t>(rgba[1], 3 * i + 1);
			buffer->set<uint8_t>(rgba[2], 3 * i + 2);
		}

		return buffer;
	}

	shared_ptr<Buffer> toJpegBuffer(HNode* node){

		auto cunode = node->cunode;

		vector<Point> points;
		points.insert(points.end(), cunode->points, cunode->points + cunode->numPoints);
		points.insert(points.end(), cunode->voxels, cunode->voxels + cunode->numVoxels);

		double factor = ceil(log2(sqrt(points.size())));
		int width = pow(2.0, factor);
		int height = width;

		int imgBufferSize = 3 * width * height;
		auto imgBuffer = malloc(imgBufferSize);
		uint8_t* imgData = reinterpret_cast<uint8_t*>(imgBuffer);

		memset(imgData, 0, imgBufferSize);

		for(int pointIndex = 0; pointIndex < points.size(); pointIndex++){

			uint32_t mortoncode = pointIndex;
			
			uint32_t x = 0;
			uint32_t y = 0;
			for(uint32_t bitindex = 0; bitindex < 10; bitindex++){
				uint32_t bx = (mortoncode >> (2 * bitindex + 0)) & 1;
				uint32_t by = (mortoncode >> (2 * bitindex + 1)) & 1;

				x = x | (bx << bitindex);
				y = y | (by << bitindex);
			}

			Point point = points[pointIndex];
			uint8_t r = (point.color >>  0) & 0xff;
			uint8_t g = (point.color >>  8) & 0xff;
			uint8_t b = (point.color >> 16) & 0xff;

			uint32_t pixelIndex = x + width * y;

			imgData[3 * pixelIndex + 0] = r;
			imgData[3 * pixelIndex + 1] = g;
			imgData[3 * pixelIndex + 2] = b;
		}

		// ofstream out;
		// out.open(filepath, ios::out | ios::binary);

		// static ofstream* ptrOut = nullptr;
		// ptrOut = &out;

		static vector<uint8_t> bytes;
		bytes.clear();

		auto myOutput = [](uint8_t byte){
			// const char* ptr = reinterpret_cast<const char*>(&byte);
			// ptrOut->write(ptr, 1);

			bytes.push_back(byte);
		};

		TooJpeg::writeJpeg(myOutput, imgData, width, height, true, 80);

		free(imgBuffer);

		// string filepath = path + "/" + node->name + ".jpeg";
		auto buffer = make_shared<Buffer>(bytes.size());
		memcpy(buffer->data, bytes.data(), bytes.size());

		// writeBinaryFile(filepath, *buffer);

		return buffer;
	}

	void write(){

		fs::create_directories(path);

		cout << "writer()" << endl;

		uint64_t offsetToNodeArray = offset_nodes - offset_buffer;
		CuNode* nodeArray = reinterpret_cast<CuNode*>(buffer->data_u8 + offsetToNodeArray);
		CuNode* curoot = &nodeArray[0];

		// cout << "offset_nodes: " << offset_nodes << endl;
		// cout << "offset_buffer: " << offset_buffer << endl;
		// cout << "offsetToNodeArray: " << offsetToNodeArray << endl;

		cout << "convert pointers" << endl;
		// make pointers point to host instead of cuda memory
		for(int i = 0; i < numNodes; i++){
			CuNode* node = &nodeArray[i];

			node->points = reinterpret_cast<Point*>(buffer->data_u8 + uint64_t(node->points) - offset_buffer);
			node->voxels = reinterpret_cast<Point*>(buffer->data_u8 + uint64_t(node->voxels) - offset_buffer);

			for(int j = 0; j < 8; j++){
				if(node->children[j] == nullptr) continue;

				CuNode* child = node->children[j];
				CuNode* fixed = reinterpret_cast<CuNode*>(buffer->data_u8 + uint64_t(child) - offset_buffer);
				node->children[j] = fixed;
			}
		}

		cout << "sort by morton code" << endl;
		// sort points and voxels by morton code
		for(int i = 0; i < numNodes; i++){
			CuNode* node = &nodeArray[i];

			Box cube(node->min, node->max);
			vec3 min = cube.min;
			vec3 max = cube.max;
			vec3 size = max - min;

			auto toMortonCode = [&](Point point){
				int ix = 128.0 * (point.x - node->min.x) / size.x;
				int iy = 128.0 * (point.y - node->min.y) / size.y;
				int iz = 128.0 * (point.z - node->min.z) / size.z;

				uint64_t mortoncode = morton::encode(ix, iy, iz);

				return mortoncode;
			};

			auto parallel = std::execution::par_unseq;
			std::sort(parallel, node->points, node->points + node->numPoints, [&](Point& a, Point& b){
				return toMortonCode(a) < toMortonCode(b);
			});

			std::sort(parallel, node->voxels, node->voxels + node->numVoxels, [&](Point& a, Point& b){
				return toMortonCode(a) < toMortonCode(b);
			});
		}


		

		cout << "create hnodes" << endl;
		unordered_map<string, HNode> hnodes;
		curoot->traverse("r", [&](CuNode* cunode, string name){
			HNode hnode;
			hnode.name = name;
			hnode.cunode = cunode;

			hnodes[name] = hnode;
		});

		for(auto& [name, hnode] : hnodes){
			if(hnode.name == "r") continue;

			string parentName = hnode.name.substr(0, hnode.name.size() - 1);
			char strChildIndex = hnode.name.back();
			int childIndex = strChildIndex - '0';

			HNode* parent = &hnodes[parentName];
			parent->children[childIndex] = &hnode;
			hnode.parent = parent;
		}

		int numVoxels = 0;
		int numPoints = 0;

		Box localCube;
		localCube.min = curoot->min;
		localCube.max = curoot->max;
		box.max = box.min + localCube.size();
		float spacing = box.size().x / 128.0;

		// Heidentor
		// float spacing = 0.12154687500000001;
		// Box box;
		// box.min = {-8.0960000000000001, -4.7999999999999998, 1.6870000000000001};
		// box.max = {7.4620000000000015, 10.758000000000003, 17.245000000000001};

		// compute diff colors
		// HNode* root = &hnodes["r"];
		// root->traverse([&](HNode* node){

		// 	node->diffColors.resize(node->cunode->numVoxels);
			
		// 	if(node->name == "r"){

		// 		for(int i = 0; i < node->cunode->numVoxels; i++){
		// 			Point voxel = node->cunode->voxels[i];
		// 			RGBA* rgba = (RGBA*)&voxel.color;
		// 			node->diffColors[i] = *rgba;
		// 		}

		// 	}else{

		// 		auto toVoxelIndex = [](Point voxel, CuNode* node){

		// 			int ix = clamp(128.0f * (voxel.x - node->min.x) / (node->max.x - node->min.x), 0.0f, 127.0f);
		// 			int iy = clamp(128.0f * (voxel.y - node->min.y) / (node->max.y - node->min.y), 0.0f, 127.0f);
		// 			int iz = clamp(128.0f * (voxel.z - node->min.z) / (node->max.z - node->min.z), 0.0f, 127.0f);
					
		// 			int voxelIndex = ix + iy * 128 + iz * 128 * 128;
				
		// 			return voxelIndex;
		// 		};

		// 		int i_parent = 0;
		// 		auto parent = node->parent->cunode;
		// 		for(int i_child = 0; i_child < node->cunode->numVoxels; i_child++){

		// 			Point v_parent = node->parent->cunode->voxels[i_child];
		// 			Point v_child = node->cunode->voxels[i_child];

		// 			bool isParent = toVoxelIndex(v_parent, parent) == toVoxelIndex(v_child, parent);
	
		// 			if(isParent){
		// 				// parentVoxels[i_child] = v_parent;


		// 			}else{
		// 				// repeat same loop iteration with next parent candidate
		// 				i_parent++;
		// 				i_child--;
		// 			}

		// 		}


		// 	}

		// });

		

		stringstream ssBatches;
		{
			std::locale::global(std::locale("en_US.UTF-8"));

			int batchDepth = 3;
			unordered_map<string, vector<HNode*>> batches;

			auto round = [](int number, int roundSize){
				return number - (number % roundSize);
			};

			for(auto& [name, hnode] : hnodes){
				if(hnode.cunode->numVoxels == 0) continue;

				int batchLevel = round(hnode.cunode->level, batchDepth);

				string batchName = name.substr(0, 1 + batchLevel);
				batches[batchName].push_back(&hnode);
			}

			for(auto& [batchName, nodes] : batches){

				for(auto node : nodes){
					node->voxelBuffer = toVoxelBuffer(node);
				}

			}

			for(auto& [batchName, nodes] : batches){

				int numVoxels = 0;
				int numPoints = 0;

				stringstream ssNodes;

				vector<shared_ptr<Buffer>> buffers;
				uint64_t bufferSize = 0;

				for(auto node : nodes){
					numVoxels += node->cunode->numVoxels;
					numPoints += node->cunode->numPoints;

					auto cunode = node->cunode;
		

					auto toVoxelIndex = [](Point voxel, CuNode* node){

						int ix = clamp(128.0f * (voxel.x - node->min.x) / (node->max.x - node->min.x), 0.0f, 127.0f);
						int iy = clamp(128.0f * (voxel.y - node->min.y) / (node->max.y - node->min.y), 0.0f, 127.0f);
						int iz = clamp(128.0f * (voxel.z - node->min.z) / (node->max.z - node->min.z), 0.0f, 127.0f);
						
						int voxelIndex = ix + iy * 128 + iz * 128 * 128;
					
						return voxelIndex;
					};

					vector<Point> parentVoxels(cunode->numVoxels);
					int i_parent = 0;
					for(int i_child = 0; i_child < cunode->numVoxels; i_child++){

						if(node->name == "r"){
							// for root node, make each voxel = parentVoxel
							parentVoxels[i_child] = cunode->voxels[i_child];
						}else{
							auto parent = node->parent->cunode;
							Point v_parent = node->parent->cunode->voxels[i_parent];
							Point v_child = cunode->voxels[i_child];

							bool isParent = toVoxelIndex(v_parent, parent) == toVoxelIndex(v_child, parent);

							if(isParent){
								parentVoxels[i_child] = v_parent;
							}else{
								// repeat same loop iteration with next parent candidate
								i_parent++;
								i_child--;
							}
						}

						
					}

					auto voxelBuffer = node->voxelBuffer;
					// auto voxelBuffer = toVoxelBuffer(node);

					auto jpegBuffer = toJpegBuffer(node);
					auto colBuffer = toColorBuffer(node);
					auto colCompressedBuffer = compress(colBuffer);

					auto colDiffBuffer = make_shared<Buffer>(colBuffer->size);
					for(int i_child = 0; i_child < cunode->numVoxels; i_child++){
						Point v_parent = parentVoxels[i_child];
						Point v_child = cunode->voxels[i_child];

						uint8_t* rgba_parent = (uint8_t*)&v_parent.color;
						uint8_t* rgba_child = (uint8_t*)&v_child.color;

						auto encode = [](int diff) -> uint8_t {
							int value = floor(log2f(abs(float(diff))));
							if(diff < 0){
								value = value + 8;
							}

							return value;
						};

						// colDiffBuffer->set<uint8_t>(uint8_t(rgba_parent[0] - rgba_child[0]), 3 * i_child + 0);
						// colDiffBuffer->set<uint8_t>(uint8_t(rgba_parent[1] - rgba_child[1]), 3 * i_child + 1);
						// colDiffBuffer->set<uint8_t>(uint8_t(rgba_parent[2] - rgba_child[2]), 3 * i_child + 2);
						if(node->name == "r"){
							colDiffBuffer->set<uint8_t>(rgba_child[0], 3 * i_child + 0);
							colDiffBuffer->set<uint8_t>(rgba_child[1], 3 * i_child + 1);
							colDiffBuffer->set<uint8_t>(rgba_child[2], 3 * i_child + 2);
						}else{
							colDiffBuffer->set<uint8_t>(encode(int(rgba_parent[0]) - int(rgba_child[0])), 3 * i_child + 0);
							colDiffBuffer->set<uint8_t>(encode(int(rgba_parent[1]) - int(rgba_child[1])), 3 * i_child + 1);
							colDiffBuffer->set<uint8_t>(encode(int(rgba_parent[2]) - int(rgba_child[2])), 3 * i_child + 2);
						}

					}
					auto colDiffCompressedBuffer = compress(colDiffBuffer);


					{ // DEBUG - WRITE JPEG

						Buffer& ref = *jpegBuffer;
						string filepath = path + "/" + node->name + ".jpeg";
						writeBinaryFile(filepath, ref);
					}

					if(node->cunode->numVoxels > 0)
					{ // DEBUG - WRITE CSV

						stringstream ss;

						for(int voxelIndex = 0; voxelIndex < node->cunode->numVoxels; voxelIndex++){

							auto voxel = node->cunode->voxels[voxelIndex];
							uint8_t* rgba = (uint8_t*)&voxel.color;

							ss << std::format("{}, {}, {}, {}, {}, {}", 
								voxel.x, voxel.y, voxel.z, rgba[0], rgba[1], rgba[2]);
							ss << endl;

						}

						string filepath = path + "/" + node->name + ".csv";
						writeFile(filepath, ss.str());

					}

					uint64_t voxelBufferOffset             = bufferSize;
					uint64_t jpegBufferOffset              = voxelBufferOffset         + voxelBuffer->positions->size;
					uint64_t colBufferOffset               = jpegBufferOffset          + jpegBuffer->size;
					// uint64_t colCompressedBufferOffset     = colBufferOffset           + colBuffer->size;
					// uint64_t colDiffBufferOffset           = colCompressedBufferOffset + colCompressedBuffer->size;
					// uint64_t colDiffCompressedBufferOffset = colDiffBufferOffset       + colDiffBuffer->size;

					buffers.push_back(voxelBuffer->positions);
					buffers.push_back(jpegBuffer);
					buffers.push_back(colBuffer);
					// buffers.push_back(colCompressedBuffer);
					// buffers.push_back(colDiffBuffer);
					// buffers.push_back(colDiffCompressedBuffer);

					bufferSize += voxelBuffer->positions->size;
					bufferSize += jpegBuffer->size;
					bufferSize += colBuffer->size;
					// bufferSize += colCompressedBuffer->size;
					// bufferSize += colDiffBuffer->size;
					// bufferSize += colDiffCompressedBuffer->size;

					string strNumChildVoxels = "";
					string strVoxelsPerOctant = "";
					for(int childIndex = 0; childIndex < 8; childIndex++){
						strNumChildVoxels += std::to_string(node->childNumVoxels[childIndex]) + ", ";
						strVoxelsPerOctant += std::to_string(node->voxelsPerOctant[childIndex]) + ", ";
					}

				//	R"V0G0N(
				//{{
				//	name: "{}",
				//	min : [{}, {}, {}], max: [{}, {}, {}],
				//	numPoints: {}, numVoxels: {},
				//	voxelBufferOffset: {},
				//	jpegBufferOffset: {},
				//	jpegBufferSize: {},
				//	colBufferOffset: {},
				//	colBufferSize: {},
				//	colCompressedBufferOffset: {},
				//	colCompressedBufferSize: {},
				//	colDiffBufferOffset: {},
				//	colDiffBufferSize: {},
				//	colDiffCompressedBufferOffset: {},
				//	colDiffCompressedBufferSize: {},
				//	jpegBBP: {},
				//	colBBP: {},
				//	colCompressedBPP: {},
				//	colDiffCompressedBPP: {},
				//	numChildVoxels: [{}],
				//	numVoxelsPerOctant: [{}],
				//}},
				//)V0G0N",

					string strNode = std::format(
				R"V0G0N(
				{{
					name: "{}",
					min : [{}, {}, {}], max: [{}, {}, {}],
					numPoints: {}, numVoxels: {},
					voxelBufferOffset: {},
					jpegBufferOffset: {},
					jpegBufferSize: {},
					colBufferOffset: {},
					colBufferSize: {},
					jpegBBP: {},
					colBBP: {},
					colCompressedBPP: {},
					colDiffCompressedBPP: {},
					numChildVoxels: [{}],
					numVoxelsPerOctant: [{}],
				}},
				)V0G0N", 
						node->name, 
						cunode->min.x, cunode->min.y, cunode->min.z,
						cunode->max.x, cunode->max.y, cunode->max.z,
						cunode->numPoints, cunode->numVoxels,
						voxelBufferOffset, 
						jpegBufferOffset, jpegBuffer->size,
						colBufferOffset, colBuffer->size,
						// colCompressedBufferOffset, colCompressedBuffer->size,
						// colDiffBufferOffset, colDiffBuffer->size,
						// colDiffCompressedBufferOffset, colDiffCompressedBuffer->size,
						std::format("{:.3}", 8.0 * float(jpegBuffer->size) / node->cunode->numVoxels),
						std::format("{:.3}", 8.0 * float(colBuffer->size) / node->cunode->numVoxels),
						std::format("{:.3}", 8.0 * float(colCompressedBuffer->size) / node->cunode->numVoxels),
						std::format("{:.3}", 8.0 * float(colDiffCompressedBuffer->size) / node->cunode->numVoxels),
						strNumChildVoxels, strVoxelsPerOctant
					);

					ssNodes << strNode << endl;
				}


				{ // save blob
					string filepath = path + "/" + batchName + ".batch";
					ofstream fout;
					fout.open(filepath, ios::binary | ios::out);

					for(auto buffer : buffers){
						fout.write(buffer->data_char, buffer->size);
					}

					fout.close();
				}


				string str = std::format(
					"{:<8} #nodes: {:10L}, #voxels: {:10L}, #points: {:10L}", 
					batchName, nodes.size(), numVoxels, numPoints); 

				string strBatch = std::format(
		R"V0G0N(
		{{
			name: "{}",
			numPoints: {}, numVoxels: {},
			nodes: [
		{}
			],
		}}
		)V0G0N", 
					batchName, numPoints, numVoxels, ssNodes.str()
				);

				ssBatches << strBatch << ", " << endl;

				if (numVoxels + numPoints > 300'000) {
					cout << str << endl;
				}

			}

		}

		std::locale::global(std::locale(std::locale::classic()));

		stringstream ssNodes;

		cout << "start writing nodes" << endl;
		for(auto& [name, hnode] : hnodes){

			CuNode* cunode = hnode.cunode;

			numVoxels += cunode->numVoxels;
			numPoints += cunode->numPoints;

			ssNodes << "\t\t{" << endl;
			ssNodes << "\t\t\tname: \"" << name << "\"," << endl;
			ssNodes << "\t\t\tmin: [" << cunode->min.x << ", " << cunode->min.y << ", " << cunode->min.z << "]," << endl;
			ssNodes << "\t\t\tmax: [" << cunode->max.x << ", " << cunode->max.y << ", " << cunode->max.z << "]," << endl;
			ssNodes << "\t\t\tnumPoints: " << cunode->numPoints << ", numVoxels: " << cunode->numVoxels << ", " << endl;
			ssNodes << "\t\t}," << endl;

			if(cunode->numVoxels > 0){

				// auto buffer = toVoxelBuffer(&hnode);

				// string filepath = path + "/" + hnode.name + ".voxels";
				// writeBinaryFile(filepath, *buffer);

			}else if(cunode->numPoints > 0){
				// leaf: save full point coordinates

				string filepath = path + "/" + hnode.name + ".points";

				auto buffer = toPointBuffer(&hnode);
				writeBinaryFile(filepath, *buffer);

			}

			// JPEG
			// if(cunode->numVoxels > 0){

			// 	auto buffer = toJpegBuffer(&hnode);

			// 	string filepath = path + "/" + hnode.name + ".jpeg";
			// 	writeBinaryFile(filepath, *buffer);
			// }
			
		}

		string metadata = std::format(R"V0G0N(
{{
	spacing: {},
	boundingBox: {{
		min: [{}, {}, {}],
		max: [{}, {}, {}],
	}},
	nodes: [
{}
	],
	batches: [
{}
	]
}}
		)V0G0N", 
			spacing, 
			box.min.x, box.min.y, box.min.y, 
			box.max.x, box.max.y, box.max.z,
			ssNodes.str(), ssBatches.str()
		);

		writeFile(path + "/metadata.json", metadata);

		cout << "#voxels: " << numVoxels << endl;
		cout << "#points: " << numPoints << endl;



		if(false)
		nodeArray[0].traverse("r", [&](CuNode* node, string name){

			vector<Point> points;
			points.insert(points.end(), node->points, node->points + node->numPoints);
			points.insert(points.end(), node->voxels, node->voxels + node->numVoxels);

			Box cube(node->min, node->max);
			vec3 min = cube.min;
			vec3 max = cube.max;
			vec3 size = max - min;

			auto toMortonCode = [&](Point point){
				int ix = 128.0 * (point.x - min.x) / size.x;
				int iy = 128.0 * (point.y - min.y) / size.y;
				int iz = 128.0 * (point.z - min.z) / size.z;

				uint64_t mortoncode = morton::encode(ix, iy, iz);

				return mortoncode;
			};

			std::sort(points.begin(), points.end(), [&](Point& a, Point& b){
				return toMortonCode(a) < toMortonCode(b);
			});

			{
				stringstream ss;

				for(int i = 0; i < points.size(); i++){

					Point point = points[i];

					uint32_t color = point.color;

					if(point.x < min.x || point.x > max.x)
					if(point.y < min.y || point.y > max.y)
					if(point.z < min.z || point.z > max.z)
					{
						color = 0x000000ff;
					}

					ss << point.x << ", " << point.y << ", " << point.z << ", ";
					ss << ((color >>  0) & 0xFF) << ", ";
					ss << ((color >>  8) & 0xFF) << ", ";
					ss << ((color >> 16) & 0xFF) << endl;
				}

				string file = path + "/" + name + ".csv";
				writeFile(file, ss.str());
			}
		});






	}

};