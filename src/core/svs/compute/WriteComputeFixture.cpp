#include <filesystem>
#include <fstream>

#include "ComputeFixture.h"
int main(int argc, char** argv)
{
	if (argc != 2)
	{
		return 2;
	}
	const auto bytes = svsc::fixtureModel();
	std::ofstream file(std::filesystem::u8path(argv[1]), std::ios::binary);
	file.write(bytes.data(), bytes.size());
	return file ? 0 : 1;
}
