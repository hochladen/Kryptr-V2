#include "stdafx.h"

//#define COMPATIBLE_WITH_V1

//Store default values of config data
std::map<std::string, void*> config{
	{"useCompression", reinterpret_cast<void*>(new bool(true))},
	{"useFullPower", reinterpret_cast<void*>(new bool(true))},
	{"deleteOriginals", reinterpret_cast<void*>(new bool(true))},
	{"scrambleName", reinterpret_cast<void*>(new bool(false))},
	{"compressionLevel", reinterpret_cast<void*>(new int(5))},
	{"chunkLimit", reinterpret_cast<void*>(new int(50000000))},
	{"noiseSeed", reinterpret_cast<void*>(new int(10271))},
	{"privateKey", reinterpret_cast<void*>(new const char*(""))}
};

template<typename T>
T getConfigVal(const char *key) {
	try {
		return *(reinterpret_cast<T*>(config[key]));
	}
	catch (std::exception e) {
		std::cout << "\n\nERROR: Config file corrupted. Please fix any recent changes.";
		std::cin.ignore(1);
		throw e;
	}
}

template<typename T>
void setConfigVal(const char* key, T value) {
	config[key] = reinterpret_cast<void*>(new T(value));
}

//Nice cross-platform way to ensure the user is ready to exit.
void waitForExit() {
	std::cin.rdbuf();
	std::cout << "\n\n\n\nPress CTRL+C to close this window." <<
		"\n----------------------------------";
	std::cin.ignore(LLONG_MAX); //LLONG_MAX is equivalent to std::numeric_limits<std::streamsize>::max
}

//Mutex used for all I/O operations.
std::mutex fileMutex;

//Some globals for encryption/decryption
std::string baseDirectory;
unsigned int chunkSize;
size_t TRASH = 10271;

bool isKV2File(const std::string& file) {
//Support V1 where KV2 files were identified with file extension only
#ifdef COMPATIBLE_WITH_V1
	if (path(file).extension() == ".kv2") {
		return true;
	}
#endif
	char buffer[4];
	std::ifstream reader;
	reader.open(file, reader.binary);
	reader.seekg(-3, std::ofstream::end);
	reader.read(buffer, 4);
	return (std::string(buffer) == "kv2");
}

void addMarker(const std::string& file) {
#ifndef COMPATIBLE_WITH_V1
	std::ofstream writer;
	writer.open(file, writer.app | writer.binary);
	writer.write("kv2", 3);
#endif
}

void getContent(const std::string& file, std::vector<size_t>& counts, std::string& out) {
	
	std::unique_lock<std::mutex> lock(fileMutex);

	std::ifstream content(file, std::ios::binary);
	std::string line;
	char* current;
	size_t linenum = 0;
	size_t pos = 0;
	while (std::getline(content, line)) //Read unpacked KV2 file line-by-line
	{
		switch (linenum) {
		case 1:
			//Get data segment lengths
			current = strtok(const_cast<char*>(line.c_str()), ".");
			while (current != nullptr) {
				counts.resize(counts.size() + 1);
				counts[pos] = std::stoi(current);
				pos++;
				current = strtok(nullptr, ".");
			}
			delete current;
			break;
		case 2:
			//Grab encrypted data
			out = line;
			break;
		}
		linenum++;
	}

	lock.unlock();
}

std::vector<std::string> split(const std::string& str, const std::string& sep) {
	char* cstr = const_cast<char*>(str.c_str());
	char* current;
	std::vector<std::string> arr;
	current = strtok(cstr, sep.c_str());
	while (current != nullptr) {
		arr.push_back(current);
		current = strtok(nullptr, sep.c_str());
	}
	return arr;
}

void readFile(const path& FilePath, std::stringstream& ss) {

	std::unique_lock<std::mutex> lock(fileMutex);

	std::ifstream ifs(FilePath, std::ios::binary);
	ss << ifs.rdbuf();

	lock.unlock();

}

void writeFile(const std::string& path, const std::string& content) {

	std::unique_lock<std::mutex> lock(fileMutex);

	std::ofstream of;
	of.open(path, std::ofstream::binary | std::ofstream::trunc);
	of << content;
	of.close();
	of.flush();

	lock.unlock();

}

std::vector<path> chunkFile(const path& filePath) {

	std::unique_lock<std::mutex> lock(fileMutex);

	std::ifstream ifs(filePath, std::ifstream::binary);
	std::ofstream ofs;

	char* buffer = new char[chunkSize];

	// Keep reading until end of file
	int counter = 1;
	std::vector<path> partFiles;

	std::cout << "\n\nSplitting a large file into chunks...";

	while (!ifs.eof()) {
		std::string newExt;
		newExt = filePath.string()
			.substr(filePath.string().length()-3, 3)
			.append(".part")
			.append(std::to_string(counter));
		path outPath = path(filePath);
		outPath.replace_extension(newExt);
		partFiles.push_back(outPath);

		// Open new chunk file name for output
		ofs.open(outPath.c_str(), std::ios::out | std::ios::trunc | std::ios::binary);

		// If chunk file opened successfully, read from input and 
		// write to output chunk. Then close.
		if (ofs.is_open()) {
			ifs.read(buffer, chunkSize);
			ofs.write(buffer, ifs.gcount());
			ofs.close();

			counter++;
		}
	}

	std::cout << "\n\nSplitting successful.";

	ifs.close();

	//If not keeping original files, delete non-chunked file
	if (getConfigVal<bool>("deleteOriginals"))
		FileShredder::ShredFile(filePath);

	//Cleanup buffer
	delete[] buffer;

	//Release file lock
	lock.unlock();

	return partFiles;
}

void mergeChunks(const std::vector<std::string>& chunks) {
	std::ofstream of;
	std::stringstream ss;
	path output;
	for (std::string file : chunks) {
		if (!isKV2File(file)||file.find(".part")==std::string::npos) continue; //Make sure chunk is encrypted and is a chunk file
		std::cout << "\n\nPutting a chunked file back together...";
		output = path(file).replace_extension("");
		path input = path(output.string());
		of.open(output.replace_extension("").string(), std::ofstream::binary | std::ofstream::app); //Open final file as append
		readFile(input, ss); //Append chunks together into original file
		of << ss.str();
		ss.str("");
		FileShredder::ShredFile(input);
		of.close();
		std::cout << "\n\nChunk merge successful.";
	}

}

void replace(std::string& x, const std::string& basetext, const std::string& newtext, const size_t& indexes = 0) {
	std::string final;
	size_t index = 0;
	for (size_t i = 0; i < x.length(); i++) {
		if (x.substr(i, basetext.length()) == basetext && (indexes == 0 || index < indexes)) {
			final += newtext;
			i += basetext.length() - 1;
			index++;
		}
		else
			final += x.at(i);
	}
	x = final;
}

std::vector<size_t> hexToIntVector(const std::string& x) {
	std::vector<size_t> out;
	out.resize(x.length() / 2);
	size_t pos = 0;
	for (size_t i = 0; i < x.length(); i += 2) {
		std::stringstream ss;
		size_t fin;
		ss << std::hex << ("0x" + x.substr(i, 2));
		ss >> fin;
		out[pos] = fin;
		pos++;
	}
	return out;
}

size_t hexToInt(const std::string& x) {
	std::stringstream ss;
	size_t fin;
	ss << std::hex << ("0x" + x);
	ss >> fin;
	return fin;
}

std::string stoh(const std::string& in)
{
	std::ostringstream os;

	for (const unsigned char& c : in)
	{
		os << std::hex << std::setprecision(2) << std::setw(2)
			<< std::setfill('0') << static_cast<size_t>(c);
	}

	std::string out = os.str();
	return out;
}

size_t conv(const std::string& in)
{
	std::ostringstream os;

	for (const unsigned char& c : in)
	{
		os << std::hex << std::setprecision(2) << std::setw(2)
			<< std::setfill('0') << static_cast<size_t>(c);
	}

	size_t out = stoi(os.str());
	return out;
}

std::string stoa(const std::string& in)
{
	std::stringstream os;
	std::string out = "";

	for (size_t i = 0; i < in.length(); i += 2)
	{
		std::unique_ptr<size_t> c = std::make_unique<size_t>();
		os << std::hex << in.substr(i, 2);
		os >> *c;
#pragma warning(suppress: 4267)
		out += *c;
	}
	return out;
}

std::string intToHex(const size_t& i, const bool& padLeft = false) {
	std::stringstream ss;
	ss << std::hex << i;
	std::string out = ss.str();
	ss.str("");
	if (padLeft)
		if (out.length() < 2) out = "0" + out;
	return out;
}

template <class T>
class MultiArray
{
public:
	std::unique_ptr<T[]> array;

	MultiArray& operator=(const MultiArray<T>& newarr) {
		delete[] array;
		array = newarr.array;
		return *this;
	}

	MultiArray() {};

	MultiArray(size_t x) {
		array = std::make_unique<T[]>(x * x * x * x);
	}
};

class Key {
public:

	constexpr Key() noexcept {};

	Key(std::string seed, bool randomize = true) {
		this->hash = picosha2::hash256_hex_string(seed);
		if (randomize) {
			const std::mt19937_64 t(hexToInt(stoa(seed)));
			std::random_device rnd;
			const std::uniform_int_distribution<size_t> dist(0, SIZE_MAX);
			this->hash = picosha2::hash256_hex_string(intToHex(dist(rnd), true) + seed);
		}
		replace(this->hash, "00", "01");
	}

	bool Initialized() const {
		return (this->hash != "");
	}

	void Initialize(std::string h) {
		this->hash = h;
		replace(this->hash, "00", "01");
	}

	std::string hash = "";
};

class Crypto {
public:

	enum PassAlgorithm { SCARY, FSEC, SQCV };

	static Key makePublic(const Key& priv, const Key& pass, const std::map<PassAlgorithm, bool>& allowedCrypts) {
		Key pubKey;
		for (const std::pair<Crypto::PassAlgorithm, bool> p : allowedCrypts) {
			if (p.second)
				pubKey.Initialize(Crypto::makeCrypt(priv, pass, p.first));
		}
		return pubKey;
	}

private:

	static std::string use_FSEC(const Key& p, const Key& pass) {
		std::string fin;
		std::vector<size_t> priv = hexToIntVector(p.hash), pa = hexToIntVector(pass.hash);
		size_t pos = 0;
		for (const size_t i : priv)
		{
			if (pos >= pa.size()) { pos = 0; }
			fin += intToHex((i > pa[pos]) ? i - pa[pos] : (i != pa[pos]) ? pa[pos] - i : 10, true);
			pos++;
		}
		return fin;
	}

	static std::string use_SCARY(const Key& p, const Key& pass) {
		std::string fin;
		std::vector<size_t> priv = hexToIntVector(p.hash), pa = hexToIntVector(pass.hash);
		size_t pos = 0;
		for (const size_t i : priv)
		{
			if (pos >= pa.size()) { pos = 0; }
			fin += intToHex(i % pa[pos] != 0 ? (i % pa[pos]) * pa[pos] % 255 : 10, true);
			pos++;
		}
		return fin;
	}

	static std::string use_SQCV(const Key& p, const Key& pass) {
		std::string fin;
		std::vector<size_t> priv = hexToIntVector(p.hash), pa = hexToIntVector(pass.hash);
		size_t pos = 0;
		for (const size_t i : priv)
		{
			if (pos >= pa.size()) { pos = 0; }
			fin += intToHex(static_cast<size_t>(std::ceil(sqrt(i + pa[pos]))), true);
			pos++;
		}
		return fin;
	}

	static std::string makeCrypt(const Key& p, const Key& pass, const PassAlgorithm& pa) {
		switch (pa) {
		case SCARY:
			return Crypto::use_SCARY(p, pass);
			break;
		case FSEC:
			return Crypto::use_FSEC(p, pass);
			break;
		case SQCV:
			return Crypto::use_SQCV(p, pass);
			break;
		default:
			return "";
			break;
		}
	}

};


class Matrix {
public:

	Matrix(size_t datasize) : arr(
				(size_t)std::ceil(std::sqrt(std::sqrt(datasize)))),
							mat_size(
				(datasize - 1 != 0) ? (size_t)std::ceil(std::sqrt(std::sqrt(datasize))) : 1) {}

	void InitKeys(const Key& p, const Key& pa) {
		this->pubkey = p;
		this->password = pa;
	}

	void Init(std::string& s) {
		s = stoh(s);
		for (size_t row = 0, column = 0, depth = 0, warp = 0, i = 0; i < s.length(); i += 2) {
			if (column >= mat_size) { column = 0; row++; }
			if (row >= mat_size) { row = 0; depth++; }
			if (depth >= mat_size) { depth = 0; warp++; }
			arr.array.get()[(column + row * mat_size) + (depth * mat_size * mat_size) + (warp * mat_size * mat_size * mat_size)] = s.substr(i, 2);
			column++;
		}
	}

	void InitFromFile(const std::string& s, const std::vector<size_t>& counts) {
		size_t pos = 0;
		for (size_t row = 0, column = 0, depth = 0, warp = 0, i = 0; i < s.length();) {
			if (column >= mat_size) { column = 0; row++; }
			if (row >= mat_size) { row = 0; depth++; }
			if (depth >= mat_size) { depth = 0; warp++; }
			arr.array.get()[(column + row * mat_size) + (depth * mat_size * mat_size) + (warp * mat_size * mat_size * mat_size)] = s.substr(i, counts[pos]);
			i += counts[pos];
			pos++;
			column++;
		}
	}

	template <class T>
	void Shift(const MultiArray<T>& data, const size_t shift = 0, bool reverse = false) {
		MultiArray<T> copy(mat_size);

		for (size_t i = 0; i < mat_size * mat_size * mat_size * mat_size; i++)
			copy.array.get()[i] = data.array.get()[i];

		for (size_t i = 0; i < mat_size; i++) {
			for (size_t g = 0; g < mat_size; g++) {
				for (size_t h = 0; h < mat_size; h++) {
					for (size_t t = 0; t < mat_size; t++) {
						if (shift != 0) {
							size_t index = (t + shift) % mat_size;
							size_t unshifted = (i + g * mat_size) + (mat_size * mat_size * h) + (mat_size * mat_size * mat_size * t);
							size_t shifted = (i + g * mat_size) + (mat_size * mat_size * h) + (mat_size * mat_size * mat_size * index);
							(reverse) ? data.array.get()[unshifted] = copy.array.get()[shifted] : data.array.get()[shifted] = copy.array.get()[unshifted];
						}
					}
				}
			}
		}
	}

	void UnravelAndWrite(const path& outPath, bool encrypting) {

		std::unique_lock<std::mutex> lock(fileMutex);

		std::ofstream of;
		of.open(outPath, std::ofstream::binary | std::ofstream::trunc);

		if (encrypting) {
			of << "KV2\n" << this->GetSizes() << "\n";

			for (size_t row = 0, column = 0, depth = 0, warp = 0; warp < mat_size;) {
				of << arr.array.get()[(column + row * mat_size) + (depth * mat_size * mat_size) + (warp * mat_size * mat_size * mat_size)];
				column++;
				if (column >= mat_size) { column = 0; row++; }
				if (row >= mat_size) { row = 0; depth++; }
				if (depth >= mat_size) { depth = 0; warp++; }
			}
		}
		else {
			for (size_t row = 0, column = 0, depth = 0, warp = 0; warp < mat_size;) {
				of << stoa(arr.array.get()[(column + row * mat_size) + (depth * mat_size * mat_size) + (warp * mat_size * mat_size * mat_size)]);
				column++;
				if (column >= mat_size) { column = 0; row++; }
				if (row >= mat_size) { row = 0; depth++; }
				if (depth >= mat_size) { depth = 0; warp++; }
			}
		}
		of.flush();
		of.close();

		lock.unlock();

	}

	void FillSpaces() {
		for (size_t row = 0, column = 0, depth = 0, warp = 0; warp < mat_size;) {
			std::string& cell = arr.array.get()[(column + row * mat_size) + (depth * mat_size * mat_size) + (warp * mat_size * mat_size * mat_size)];
			cell = cell.length() >= 2 || cell == "" ? cell : "0" + cell;
			column++;
			if (column >= mat_size) { column = 0; row++; }
			if (row >= mat_size) { row = 0; depth++; }
			if (depth >= mat_size) { depth = 0; warp++; }
		}
	}

	std::string GetSizes() {
		std::string fin = "";
		for (size_t row = 0, column = 0, depth = 0, warp = 0; warp < mat_size;) {
			std::string& cell = arr.array.get()[(column + row * mat_size) + (depth * mat_size * mat_size) + (warp * mat_size * mat_size * mat_size)];
			fin += cell != "" ? std::to_string(cell.length()) + "." : "";
			column++;
			if (column >= mat_size) { column = 0; row++; }
			if (row >= mat_size) { row = 0; depth++; }
			if (depth >= mat_size) { depth = 0; warp++; }
		}
		return fin;
	}

	void Shuffle() {
		this->Crypt();
	}

	void DeShuffle() {
		this->undoCrypt();
	}

private:
	Key pubkey, password;
	MultiArray<std::string> arr;
	const size_t mat_size;

	void Crypt() {


		std::vector<size_t> pass = hexToIntVector(this->password.hash);
		std::vector<size_t> pub = hexToIntVector(this->pubkey.hash);

		size_t const passSize = pass.size(), const pubSize = pub.size();

		MultiArray<size_t> temp(mat_size);


		for (size_t row = 0, column = 0, depth = 0, warp = 0, keypos = 0; warp < mat_size;) {
			size_t index = (column + row * mat_size) + (depth * mat_size * mat_size) + (warp * mat_size * mat_size * mat_size);
			std::string const * cell = &arr.array.get()[index];
			size_t* tempcell = &temp.array.get()[index];
			const size_t intcell = hexToInt(*cell);
			const size_t* passval = &pass[keypos];
			const size_t* pubval = &pub[keypos];
			*tempcell = ((*cell != "") ? (((row % 2 == 0 && depth % 2 == 1) || (row % 2 == 1 && depth % 2 == 0)) ? *passval * intcell : *pubval * intcell) : ((row % 2 == 0) ? *passval * TRASH : *pubval * TRASH));
			column++, keypos++;
			if (column >= mat_size) { column = 0; row++; }
			if (row >= mat_size) { row = 0; depth++; }
			if (depth >= mat_size) { depth = 0; warp++; }
			if (keypos >= pass.size()) { keypos = 0; }
		}

		this->Shift<size_t>(temp, pass[mat_size % passSize], true);

		for (size_t row = 0, column = 0, depth = 0, warp = 0, keypos = 0; warp < mat_size;) {
			size_t index = (column + row * mat_size) + (depth * mat_size * mat_size) + (warp * mat_size * mat_size * mat_size);
			std::string* cell = &arr.array.get()[index];
			const size_t* passval = &pass[keypos];
			const size_t* pubval = &pub[keypos];
			size_t const intcell = temp.array.get()[index];
			*cell = ((((column % 2 == 0 && depth % 2 == 1) || (column % 2 == 1 && depth % 2 == 0)) ? intToHex(*passval * intcell) : intToHex(*pubval * intcell)));
			column++, keypos++;
			if (column >= mat_size) { column = 0; row++; }
			if (row >= mat_size) { row = 0; depth++; }
			if (depth >= mat_size) { depth = 0; warp++; }
			if (keypos >= pass.size()) { keypos = 0; }
		}
		this->Shift<std::string>(arr, pub[mat_size % pubSize]);
	}

	void undoCrypt() {
		std::vector<size_t> pass = hexToIntVector(this->password.hash);
		std::vector<size_t> pub = hexToIntVector(this->pubkey.hash);

		const size_t passSize = pass.size(), const pubSize = pub.size();

		MultiArray<size_t> temp(mat_size);

		this->Shift<std::string>(arr, pub[mat_size % pubSize], true);

		for (size_t row = 0, column = 0, depth = 0, warp = 0, keypos = 0; warp < mat_size;) {
			size_t index = (column + row * mat_size) + (depth * mat_size * mat_size) + (warp * mat_size * mat_size * mat_size);
			const size_t* passval = &pass[keypos];
			const size_t* pubval = &pub[keypos];
			const std::string* cell = &arr.array.get()[index];
			size_t* tempcell = &temp.array.get()[index];
			const size_t intcell = hexToInt(*cell);
			*tempcell = (((column % 2 == 0 && depth % 2 == 1) || (column % 2 == 1 && depth % 2 == 0)) ? intcell / (*passval) : intcell / (*pubval));
			column++, keypos++;
			if (column >= mat_size) { column = 0; row++; }
			if (row >= mat_size) { row = 0; depth++; }
			if (depth >= mat_size) { depth = 0; warp++; }
			if (keypos >= pass.size()) { keypos = 0; }
		}

		this->Shift<size_t>(temp, pass[mat_size % passSize]);

		for (size_t row = 0, column = 0, depth = 0, warp = 0, keypos = 0; warp < mat_size;) {
			size_t index = (column + row * mat_size) + (depth * mat_size * mat_size) + (warp * mat_size * mat_size * mat_size);
			std::string* cell = &arr.array.get()[index];
			const size_t intcell = temp.array.get()[index];
			const size_t* passval = &pass[keypos];
			const size_t* pubval = &pub[keypos];
			*cell = ((intcell / (*pubval) != TRASH && intcell / (*passval) != TRASH) ? (((row % 2 == 0 && depth % 2 == 1) || (row % 2 == 1 && depth % 2 == 0)) ? intToHex(intcell / (*passval)) : intToHex(intcell / (*pubval))) : "");
			column++, keypos++;
			if (column >= mat_size) { column = 0; row++; }
			if (row >= mat_size) { row = 0; depth++; }
			if (depth >= mat_size) { depth = 0; warp++; }
			if (keypos >= pass.size()) { keypos = 0; }
		}
	}

};

//Crypt config
std::map<Crypto::PassAlgorithm, bool> allowedCrypts;

//Keys
Key privateKey;
Key passwordHash;
Key publicKey;


void readConfig() {
	std::stringstream ss;
	rapidjson::Document d;
	readFile(baseDirectory + "\\settings.json", ss);
	const std::string cfgcontent = ss.str();
	d.Parse(cfgcontent.c_str());
	//Cycle through all config settings
	for (std::pair<std::string, void*> configval : config) {
		const std::string &key = configval.first;
		rapidjson::Value& value = d[key.c_str()];
		if (value.IsBool()) {
			setConfigVal<bool>(key.c_str(), value.GetBool());
		}
		else if (value.IsInt()) {
			setConfigVal<unsigned int>(key.c_str(), value.GetInt());
		}
		else if (value.IsString()) {
			setConfigVal<const char*>(key.c_str(), value.GetString());
		}
	}
	chunkSize = getConfigVal<int>("chunkLimit") * 1000000;
	TRASH = getConfigVal<int>("noiseSeed");
	if (getConfigVal<const char*>("privateKey") != "")
		privateKey.Initialize(getConfigVal<const char*>("privateKey"));
	
	allowedCrypts = {
		{ Crypto::PassAlgorithm::SCARY, d["crypts"]["SCARY"].GetBool()},
		{ Crypto::PassAlgorithm::FSEC, d["crypts"]["FSEC"].GetBool() },
		{ Crypto::PassAlgorithm::SQCV, d["crypts"]["SQCV"].GetBool() }
	};
}

//Mutex for ensuring threads don't overlap during encryption & decryption
std::mutex kvmutex;


std::string encrypt(path file, bool mult) {

	bool fileIsChunk = (file.string().find(".part") != std::string::npos);
	if (fileIsChunk) setConfigVal<bool>("useCompression", false);
	std::string seed;
	std::stringstream ss;
	bit7z::Bit7zLibrary lib;
	bit7z::BitCompressor compressor(lib, bit7z::BitFormat::SevenZip);
	compressor.setCompressionLevel(static_cast<bit7z::BitCompressionLevel>(getConfigVal<int>("compressionLevel")));
	std::vector<std::wstring> files = { file.wstring() };
	if (getConfigVal<bool>("useCompression")) {
		compressor.compress(files, file.wstring() + L".kc");

		file += L".kc";
	}

	std::unique_lock<std::mutex> lock(kvmutex);

	if (!privateKey.Initialized()) {
		std::cout << "\n\nInput a seed to generate your private key OR press ENTER to use an existing key: ";
		std::getline(std::cin, seed);
		if (seed == "") {
			if (getConfigVal<const char*>("privateKey") != "") {
				std::cout << "\n\nDetected a pre-set private key in settings.cfg. Using that as private key.";
				seed = getConfigVal<const char*>("privateKey");
			}
			else {
				std::cout << "\n\nInput your existing private key: ";
				std::getline(std::cin, seed);
			}
			privateKey.Initialize(seed);
		}
		else privateKey = Key(seed, true);
	}
	if (!passwordHash.Initialized()) {
		if (getConfigVal<const char*>("privateKey") != "") {
			std::cout << "\n\nDetected a pre-set password in settings.cfg. Using that as password.";
			seed = getConfigVal<const char*>("privateKey");
		}
		else {
			std::cout << "\n\nInput a password to lock the file(s) with: ";
			std::getline(std::cin, seed);
		}
		passwordHash = Key(seed, false);
	}
	if (!publicKey.Initialized()) {
		publicKey = Crypto::makePublic(privateKey, passwordHash, allowedCrypts);
	}

	lock.unlock();

	std::cout << "\n\nReading file(s).... this can take a while.";
	readFile(file, ss);
	std::string input = ss.str();
	ss.str("");

	if (getConfigVal<bool>("useCompression"))
		FileShredder::ShredFile(file);

	Matrix m(input.size());
	m.Init(input);
	m.InitKeys(publicKey, passwordHash);
	std::cout << "\n\nPerforming encryption....";
	m.Shuffle();
	std::cout << "\n\nWriting encrypted data to output....";

	if (!getConfigVal<bool>("useCompression")) file += ".kc";

	m.UnravelAndWrite(file, true);

	files = { file.wstring() };

	file.replace_extension(".kv2");


	if (exists(file.wstring())) FileShredder::ShredFile(file.wstring());

	compressor.setPassword(path(Key(publicKey.hash, false).hash).wstring());
	compressor.compress(files, file.wstring());

	addMarker(file.string());

	FileShredder::ShredFile(files[0]);

	return file.string();
}

std::string decrypt(path file, bool mult) {
		bool isCompressed = true;
		bool fileIsChunk = (file.string().find(".part") != std::string::npos);
		std::vector<size_t> counts;
		std::string input;
		std::string seed;
		bit7z::Bit7zLibrary lib;
		bit7z::BitExtractor extractor(lib, bit7z::BitFormat::SevenZip);
		std::ifstream reader;

		std::unique_lock<std::mutex> lock(kvmutex);

		if (!publicKey.Initialized()) {
			std::cout << "\n\nInput your public key: ";
			std::getline(std::cin, seed);
			publicKey.Initialize(seed);
		}
		if (!passwordHash.Initialized()) {
			std::cout << "\n\nInput your password: ";
			std::getline(std::cin, seed);
			passwordHash = Key(seed, false);
		}

		lock.unlock();

		extractor.setPassword(path(Key(publicKey.hash, false).hash).wstring());
		extractor.extract(file.wstring(), file.parent_path());

		file.replace_extension(".kc");

		std::cout << "\n\nReading encrypted file.... this can take a while.";
		getContent(file.string(), counts, input);
		FileShredder::ShredFile(file);
		Matrix m(size_t(counts.size()));
		m.InitFromFile(input, counts);
		m.InitKeys(publicKey, passwordHash);
		std::cout << "\n\nPerforming decryption....";
		m.DeShuffle();
		m.FillSpaces();
		std::cout << "\n\nWriting decrypted data to output....";

		m.UnravelAndWrite(file, false);

		char* buffer = new char[3];
		reader.open(file.string(), std::ifstream::binary);
		reader.get(buffer, 3);
		reader.close();

		if (std::string(buffer) != "7z") isCompressed = false;

		delete[] buffer;

		if (isCompressed) {
			extractor.clearPassword();
			extractor.extract(file.wstring(), file.parent_path());
			FileShredder::ShredFile(file);
		}
		else
		{
			rename(file, path(file).replace_extension(""));
		}

		return file.replace_extension("").string();
}

void encrypt_from_list(std::vector<std::string> &filelist) {

	std::vector<std::string> paths;

	for (size_t listindex = 0; listindex < filelist.size(); listindex++) {
		std::string file = filelist[listindex];
		if (is_directory(file)) {
			std::stringstream ss;
			for (std::filesystem::directory_entry p : recursive_directory_iterator(file)) {
				if (!is_directory(p)) {
					ss << p.path();
					paths.push_back(ss.str());
					ss.str("");
				}
			}
		}
		else paths.push_back(file);
	}

	std::string input;

	std::atomic<int> pos = 0;

	int internal_counter = 0;

	unsigned int hConcurrence = std::thread::hardware_concurrency();

	const unsigned int max_threads = (hConcurrence ? hConcurrence : 1);

	std::vector<std::string> chunkedpaths;
	std::vector<std::thread> threads;

	for (std::string file : paths) {

		std::string oldname;
		bool keepScramble = false;

		if (getConfigVal<bool>("scrambleName") && !isKV2File(file)) {

			keepScramble = true;

			oldname = std::string(file);

			std::string name;
			path basepath = path(file);
			path newpath;
			do {
				newpath = path(file);
				name = static_cast<path>(std::tmpnam(nullptr)).filename().string();
			} while (exists(newpath.replace_filename(name)));
			rename(basepath.c_str(), newpath);

			file = newpath.string();
		}

		std::ifstream in(file, std::ifstream::ate | std::ifstream::binary);
		if (!isKV2File(file) && (size_t)in.tellg() > chunkSize) {
			in.close();
			for (path p : chunkFile(file))
				chunkedpaths.push_back(p.string());

			if (keepScramble) { //Return files back to original name after splitting is done, if they are preserved and scrambled
				const int res = rename(file.c_str(), oldname.c_str());
				if (res != 0)
					std::cout << "\n\nFAILED TO RENAME FILE AT \"" << oldname.c_str() << "\", ORIGINAL FILE NAMES MAY NOT BE PRESERVED.";
			}

		}
		else chunkedpaths.push_back(file);
	}

	std::sort(chunkedpaths.begin(), chunkedpaths.end());

	for (auto file : chunkedpaths) {

		bool encryptedFile = false;

		threads.push_back(std::thread([file, chunkedpaths, &encryptedFile, &pos] {

			bool enc = false;

			std::string pa;
			if (!isKV2File(file)) {

				pa = encrypt(file, chunkedpaths.size() != 1);

				if (!encryptedFile) encryptedFile = true;
				enc = true;
			}
			else {
				pa = decrypt(file, chunkedpaths.size() != 1);
			}
			if (getConfigVal<bool>("deleteOriginals") || (path(file).string().find(".part") != std::string::npos && !isKV2File(file)))
				FileShredder::ShredFile(file);

			std::cout << "\n\nSuccessfully " << ((enc) ? std::string("encrypted") : std::string("decrypted")) << " file " << std::to_string(++pos) << "/" << std::to_string(chunkedpaths.size()) << " to " << pa;

			}));

		if ((threads.size() >= max_threads / 2 && !getConfigVal<bool>("useFullPower")) || threads.size() == max_threads) { //If thread count has reached a maximum, join currently running threads before continuing
			for (std::thread& t : threads) {
				t.join();
			}
			threads.clear();
		}
		if (internal_counter == chunkedpaths.size() - 1) { //If queue is out of files to process, join all threads and finish
			for (std::thread& t : threads) {
				t.join();
			}
			threads.clear();
			mergeChunks(chunkedpaths);
			std::cout << "\n\nFINISHED!";
			if (encryptedFile) {
				std::cout << "\n\nYour public key is: " << publicKey.hash << "\nSend it to the person(s) you wish to be able to decrypt the file(s), along with the password you locked it with.";
				std::cout << "\n\nYour private key is: " << privateKey.hash << "\nKEEP THIS SOMEWHERE SAFE. You will only be able to generate another public key by knowing this and the password.";
			}
			waitForExit();
		}
		internal_counter++;
	}
}

bool all_files_exist(std::vector<std::string> filelist) {
	for (std::string file : filelist) {
		if (!exists(file)) {
			std::cout << "\nFILEPATH \"" << std::string(file) << "\" DOES NOT EXIST. PLEASE RESOLVE TO CONTINUE.";
			return false;
		}
	}
	return true;
}

void recoverPublicKey() {
	std::string stringinput;
	std::cout << "\n\nNOTE: Key recovery will only work if using the same encryption method!";
	std::cout << "\n\nInput the private key that was generated: ";
	std::getline(std::cin, stringinput);
	Key priv;
	priv.Initialize(stringinput);
	std::cout << "\n\nInput the password that was used: ";
	std::getline(std::cin, stringinput);
	Key pass(stringinput, false);
	std::cout << "\n\nRecovering public key...";
	Key pub = Crypto::makePublic(priv, pass, allowedCrypts);
	std::cout << "\n\nPublic key is: " + pub.hash;
	waitForExit();
}

void encryptString() {
	std::string stringinput;
	std::string file;
	std::cout << "\n\nEnter text to encrypt: ";
	std::getline(std::cin, stringinput);
	std::cout << "\n\nSet output file path: ";
	std::getline(std::cin, file);
	writeFile(file, stringinput);
	std::string fileName = encrypt(file, false);
	if (getConfigVal<bool>("deleteOriginals"))
		FileShredder::ShredFile(file);
	std::cout << "\n\nFINISHED.";
	std::cout << "\n\nYour public key is: " << publicKey.hash << "\nSend it to the person(s) you wish to be able to decrypt the file(s), along with the password you locked it with.";
	std::cout << "\n\nYour private key is: " << privateKey.hash << "\nKEEP THIS SOMEWHERE SAFE. You will only be able to generate another public key by knowing this and the password.";
	waitForExit();
}


int main(size_t argc, char** argv)
{
	std::vector<std::string> filelist;

	//We want our streams to be speedy, thanks
	//std::ios_base::sync_with_stdio(false);

	path filepath = argv[0];

	baseDirectory = filepath.remove_filename().string();
	readConfig();
	std::string	inputs;

	if (argv[1] != nullptr && argv[1] == std::string("GUIHOOK"))
	{
		inputs = argv[2];
		if (argv[3] != std::string("$NOSEED$")) {
			Key priv(argv[3], true);
			privateKey = priv;
		}
		Key pass(argv[4], false);
		passwordHash = pass;
		if (argv[5] != std::string("$NOPUB$"))
			publicKey.Initialize(argv[5]);
		if (argv[6] == std::string("$NOCOMPRESS$"))
			setConfigVal<bool>("useCompression", false);
		if (argv[7] == std::string("$NOFULL$"))
			setConfigVal<bool>("useFullPower", false);
		if (argv[8] == std::string("$KEEP$"))
			setConfigVal<bool>("deleteOriginals", false);
		setConfigVal<unsigned int>("compressionLevel", std::stoi(argv[9]));
		if (argv[10] == std::string("$SCRAMBLE$"))
			setConfigVal<bool>("scrambleName", true);

		filelist = split(inputs, ",");
		encrypt_from_list(filelist);

	}
	else if (argv[1] != nullptr && argv[1] == std::string("RECOVERYHOOK")) {
		recoverPublicKey();
	}
	else if (argv[1] != nullptr && argv[1] == std::string("STRINGHOOK")) {
		encryptString();
	}
	else {
		//Handle shell
	}

	return 0;
}