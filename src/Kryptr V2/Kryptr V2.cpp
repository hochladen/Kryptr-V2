#include "stdafx.h"

//#define COMPATIBLE_WITH_V1

//Store default values of config data

#define typeless(x, y) reinterpret_cast<void*>(new x(y))

std::map<const char*, void*> config{
	{"useCompression", typeless(bool, true)},
	{"useFullPower", typeless(bool, true)},
	{"deleteOriginals", typeless(bool, true)},
	{"scrambleName", typeless(bool, false)},
	{"compressionLevel", typeless(int, 5)},
	{"chunkLimit", typeless(int, 50)},
	{"noiseSeed", typeless(int, 10271)},
	{"privateKey", typeless(const char*, "")},
	{"IOBufferSize", typeless(int, 64000)},
	{"giveSSDWarning", typeless(bool, true)}
};

template<typename T>
T getConfigVal(const char* key) {
	try {
		return *(reinterpret_cast<T*>(config[key]));
	}
	catch (std::exception e) {
		throw std::exception("ERROR: Config file corrupted. Please fix any recent changes.");
	}
}

template<typename T>
void setConfigVal(const char* key, T value) {
	if (config.find(key) != config.end())
		delete config[key];
	config[key] = reinterpret_cast<void*>(new T(value));
}

//Nice cross-platform way to ensure the user is ready to exit.
void waitForExit() {
	Colors::TextColor(WHITE, BLACK);
	std::cout << "\n\n\n\nPress CTRL+C to close this window." <<
		"\n----------------------------------";
	std::cin.ignore(LLONG_MAX); //LLONG_MAX is equivalent to std::numeric_limits<std::streamsize>::max
}

//Mutex used for all I/O operations.
static std::mutex fileMutex;

//Some globals for encryption/decryption
static std::string baseDirectory;
static unsigned int chunkSize;
static bool giveSSDWarning;
static size_t TRASH = 10271;

bool isKV2File(const path& file) {
	//Support V1 where KV2 files were identified with file extension only
#ifdef COMPATIBLE_WITH_V1
	if (path(file).extension() == ".kv2") {
		return true;
	}
#endif
	char buffer[4];
	std::ifstream reader;
	reader.open(file, std::ifstream::binary);
	reader.seekg(-3, std::ofstream::end);
	reader.get(buffer, 4);
	return (std::string(buffer) == "kv2");
}

void addMarker(const path& file) {
	std::ofstream writer;
	writer.open(file, std::ofstream::app | std::ofstream::binary);
	writer.write("kv2", 3);
}

template <typename T = std::string>
std::vector<T> split(const std::string& str, const std::string& sep) {
	char* cstr = const_cast<char*>(str.c_str());
	char* current;
	std::vector<T> arr;
	current = strtok(cstr, sep.c_str());
	while (current != nullptr) {
		arr.push_back(T(current));
		current = strtok(nullptr, sep.c_str());
	}
	delete current;
	return arr;
}

void getContent(const path& file, std::vector<size_t>& counts, std::string& out) {

	std::unique_lock<std::mutex> lock(fileMutex);

	std::ifstream ifs(file, std::ios::binary);
	std::string line;
	size_t linenum = 0;
	while (std::getline(ifs, line)) //Read unpacked KV2 file line-by-line
	{
		switch (linenum) {
		case 1:
			//Get data segment lengths
			for (std::string n : split(line, ".")) {
				counts.push_back(std::stoi(n));
			}
			break;
		case 2:
			//Grab encrypted data
			out = line;
			break;
		}
		++linenum;
	}
	lock.unlock();
}

void readFile(const path& FilePath, std::stringstream& ss) {

	std::unique_lock<std::mutex> lock(fileMutex);

	HANDLE fileHandle = CreateFile(FilePath.wstring().c_str(), GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

	if (fileHandle == INVALID_HANDLE_VALUE)
		throw std::exception(std::string("Could not open file \"" + FilePath.string() + "\" for reading.").c_str());

	int bufferSize = getConfigVal<int>("IOBufferSize");
	unsigned long bytesRead = 1;
	char* buffer = reinterpret_cast<char*>(VirtualAlloc(NULL, bufferSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
	while (bytesRead !=0) {
		ReadFile(fileHandle, buffer, bufferSize, &bytesRead, NULL);
		ss.write(buffer, bytesRead);
	}

	CloseHandle(fileHandle);
	VirtualFree(buffer, 0, MEM_RELEASE);

	lock.unlock();
}

void writeFile(const path& FilePath, std::stringstream& contentReader, bool append = false) {

	std::unique_lock<std::mutex> lock(fileMutex);

	int bufferSize = getConfigVal<int>("IOBufferSize");

	HANDLE fileHandle = CreateFile(FilePath.wstring().c_str(), GENERIC_WRITE, 0, NULL, append ? OPEN_ALWAYS : CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (append) SetFilePointer(fileHandle, 0, NULL, FILE_END);

	if (fileHandle == INVALID_HANDLE_VALUE)
		throw std::exception(std::string("Could not open file \"" + FilePath.string() + "\" for writing.").c_str());

	unsigned long bytesWritten = 0;
	unsigned long bytesToWrite = 0;
	char* buffer = reinterpret_cast<char*>(VirtualAlloc(NULL, bufferSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));

	while (!contentReader.eof()) {
		contentReader.read(buffer, bufferSize);
		bytesToWrite = contentReader.gcount();
		WriteFile(fileHandle, buffer, bytesToWrite, &bytesWritten, NULL);
		if (bytesWritten < bytesToWrite) throw std::exception(std::string("Error while writing to file \"" + FilePath.string() + "\"").c_str());
	}

	CloseHandle(fileHandle);
	VirtualFree(buffer, 0, MEM_RELEASE);

	lock.unlock();

}

std::vector<path> chunkFile(const path& filePath) {

	HANDLE fileHandle = CreateFile(filePath.wstring().c_str(), GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

	if (fileHandle == INVALID_HANDLE_VALUE)
		throw std::exception(std::string("Could not open file \"" + filePath.string() + "\" for reading.").c_str());

	char* buffer = reinterpret_cast<char*>(VirtualAlloc(NULL, chunkSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));

	// Keep reading until end of file
	int counter = 1;
	std::vector<path> partFiles;

	std::cout << "\n\nSplitting a file into chunks...";

	unsigned long bytesRead = 1;
	while (bytesRead != 0) {
		std::string newExt;
		newExt = filePath.string()
			.substr(filePath.string().length() - 3, 3)
			.append(".part")
			.append(std::to_string(counter));
		path outPath = path(filePath).replace_extension(newExt);

		// If chunk file opened successfully, read from input and 
		// write to output chunk. Then close.
		ReadFile(fileHandle, buffer, chunkSize, &bytesRead, NULL);
		if (bytesRead != 0) {
			std::stringstream ss;
			ss.write(buffer, bytesRead);
			writeFile(outPath, ss);
			partFiles.push_back(outPath);
		}
		++counter;
	};
	Colors::TextColor(GREEN, BLACK);
	std::cout << "\n\nSplitting successful.";
	Colors::TextColor(WHITE, BLACK);

	CloseHandle(fileHandle);

	//If not keeping original files, delete non-chunked file
	if (getConfigVal<bool>("deleteOriginals"))
		FileShredder::ShredFile(filePath, giveSSDWarning);

	//Cleanup buffer
	VirtualFree(buffer, 0, MEM_RELEASE);

	return partFiles;
}

void mergeChunks(const std::vector<path>& chunks) {
	for (path file : chunks) {
		file.replace_extension("");
		if (isKV2File(file) || file.string().find(".part") == std::string::npos) continue; //Make sure file is a decrypted chunk file
		std::cout << "\n\nPutting a chunked file back together...";
		std::stringstream ss;
		readFile(file, ss);
		writeFile(path(file.string()).replace_extension(""), ss, true); //Append chunks together into original file
		FileShredder::ShredFile(file, giveSSDWarning);
		Colors::TextColor(GREEN, BLACK);
		std::cout << "\n\nChunk merge successful.";
		Colors::TextColor(WHITE, BLACK);
	}
}

void replace(std::string& source, const std::string& searchtext, const std::string& newtext, const size_t& indexes = 0) {
	std::string final;
	size_t index = 0;
	for (size_t i = 0; i < source.length(); ++i) {
		if (source.substr(i, searchtext.length()) == searchtext && (indexes == 0 || index < indexes)) {
			final += newtext;
			i += searchtext.length() - 1;
			++index;
		}
		else final += source[i];
	}
	source = final;
}

std::vector<size_t> hexToIntVector(const std::string& x) {
	std::vector<size_t> out;
	out.resize(x.length() / 2);
	size_t pos = 0;
	for (size_t i = 0; i < x.length(); i += 2) {
		std::stringstream ss;
		size_t fin;
		ss << std::hex << x.substr(i, 2);
		ss >> fin;
		out[pos] = fin;
		++pos;
	}
	return out;
}

size_t hexToInt(const std::string& x) {
	std::stringstream ss;
	size_t fin;
	ss << std::hex << x;
	ss >> fin;
	return fin;
}

std::string stringToPaddedHex(const std::string& in)
{
	std::ostringstream os;

	for (const unsigned char& c : in)
	{
		os << std::hex << std::setw(2) << std::setfill('0') << static_cast<size_t>(c);
	}
	return os.str();
}

std::string decodeString(const std::string& in)
{
	std::stringstream os;
	std::string out = "";

	for (size_t i = 0; i < in.length(); i += 2)
	{
		size_t c;
		os << std::hex << in.substr(i, 2);
		os >> c;
		out += c;
	}
	return out;
}

std::string intToHex(const size_t& i, const bool& padLeft = false) {
	std::stringstream ss;
	ss << std::hex << i;
	if (padLeft)
		if (ss.str().length() < 2) return "0" + ss.str();
	return ss.str();
}

template <typename T>
class MultiArray
{
public:
	std::unique_ptr<T[]> array;

	MultiArray& operator=(const MultiArray& newarr) {
		array = newarr.array;
		return *this;
	}

	MultiArray(size_t x) {
		array = std::make_unique<T[]>(x * x * x * x);
	}
};

class Key {

public:

	Key() noexcept {};

	Key(std::string seed, bool randomize = true) {
		this->hash = picosha2::hash256_hex_string(seed);
		if (randomize) {
			//MT is a PRNG, we can increase entropy by feeding in a seed. Distribution is not uniform.
			static std::mt19937_64 t;
			if (&t == nullptr) {
				t = std::mt19937_64(hexToInt(decodeString(seed)));
			}
			//random_device is more of a "true" RNG, also has uniform distribution
			std::random_device rnd;
			const std::uniform_int_distribution<size_t> dist(0, SIZE_MAX); //Ensure uniform distribution for MT
			//Generate final hash by salting base hash with hex string, which is generated from XOR and mask operation of RNG w/ uniform distribution
			this->hash = picosha2::hash256_hex_string(intToHex((rnd() ^ dist(t)) & 0xFFFFFFFF, true) + this->hash);
		}
		//Prevent invalid masking operations later on
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
			++pos;
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
			++pos;
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
			++pos;
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
			throw std::exception("No crypt method chosen. Please choose at least one in settings.json.");
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
			(datasize - 1 != 0) ? (size_t)std::ceil(std::sqrt(std::sqrt(datasize))) : 1),
		mat_size_2(mat_size * mat_size),
		mat_size_3(mat_size_2 * mat_size) {}

	void InitKeys(const Key& p, const Key& pa) {
		this->pubkey = p;
		this->password = pa;
	}

	void Init(std::string& s) {
		s = stringToPaddedHex(s);
		for (size_t row = 0, column = 0, depth = 0, warp = 0, i = 0; i < s.length(); i += 2) {
			if (column >= mat_size) { column = 0; ++row; }
			if (row >= mat_size) { row = 0; ++depth; }
			if (depth >= mat_size) { depth = 0; ++warp; }
			arr.array.get()[(column + row * mat_size) + (depth * mat_size_2) + (warp * mat_size_3)] = s.substr(i, 2);
			++column;
		}
	}

	void InitFromFile(const std::string& s, const std::vector<size_t>& counts) {
		size_t pos = 0;
		for (size_t row = 0, column = 0, depth = 0, warp = 0, i = 0; i < s.length();) {
			if (column >= mat_size) { column = 0; ++row; }
			if (row >= mat_size) { row = 0; ++depth; }
			if (depth >= mat_size) { depth = 0; ++warp; }
			arr.array.get()[(column + row * mat_size) + (depth * mat_size_2) + (warp * mat_size_3)] = s.substr(i, counts[pos]);
			i += counts[pos];
			++pos;
			++column;
		}
	}

	template <class T>
	void Shift(const MultiArray<T>& data, const size_t shift = 0, bool reverse = false) {
		MultiArray<T> copy(mat_size);

		for (size_t i = 0; i < mat_size_3 * mat_size; ++i)
			copy.array.get()[i] = data.array.get()[i];

		size_t index;
		size_t base_location;
		size_t unshifted;
		size_t shifted;

		for (size_t i = 0; i < mat_size; ++i) {
			for (size_t g = 0; g < mat_size; ++g) {
				for (size_t h = 0; h < mat_size; ++h) {
					for (size_t t = 0; t < mat_size; ++t) {
						if (shift != 0) {
							index = (t + shift) % mat_size;
							base_location = (i + g * mat_size) + (mat_size_2 * h);
							unshifted = base_location + (mat_size_3 * t);
							shifted = base_location + (mat_size_3 * index);
							(reverse) ? data.array.get()[unshifted] = copy.array.get()[shifted] : data.array.get()[shifted] = copy.array.get()[unshifted];
						}
					}
				}
			}
		}
	}

	void UnravelAndWrite(const path& outPath, bool encrypting) {

		std::stringstream ss;

		if (encrypting) {
			ss << "KV2\n" << this->GetSizes() << "\n";

			for (size_t row = 0, column = 0, depth = 0, warp = 0; warp < mat_size;) {
				ss << arr.array.get()[(column + row * mat_size) + (depth * mat_size_2) + (warp * mat_size_3)];
				++column;
				if (column >= mat_size) { column = 0; ++row; }
				if (row >= mat_size) { row = 0; ++depth; }
				if (depth >= mat_size) { depth = 0; ++warp; }
			}
		}
		else {
			for (size_t row = 0, column = 0, depth = 0, warp = 0; warp < mat_size;) {
				ss << decodeString(arr.array.get()[(column + row * mat_size) + (depth * mat_size_2) + (warp * mat_size_3)]);
				++column;
				if (column >= mat_size) { column = 0; ++row; }
				if (row >= mat_size) { row = 0; ++depth; }
				if (depth >= mat_size) { depth = 0; ++warp; }
			}
		}

		writeFile(outPath, ss);

	}

	void FillSpaces() {
		for (size_t row = 0, column = 0, depth = 0, warp = 0; warp < mat_size;) {
			std::string& cell = arr.array.get()[(column + row * mat_size) + (depth * mat_size_2) + (warp * mat_size_3)];
			cell = cell.length() >= 2 || cell == "" ? cell : "0" + cell;
			++column;
			if (column >= mat_size) { column = 0; ++row; }
			if (row >= mat_size) { row = 0; ++depth; }
			if (depth >= mat_size) { depth = 0; ++warp; }
		}
	}

	std::string GetSizes() {
		std::string fin = "";
		for (size_t row = 0, column = 0, depth = 0, warp = 0; warp < mat_size;) {
			std::string& cell = arr.array.get()[(column + row * mat_size) + (depth * mat_size_2) + (warp * mat_size_3)];
			fin += cell != "" ? std::to_string(cell.length()) + "." : "";
			++column;
			if (column >= mat_size) { column = 0; ++row; }
			if (row >= mat_size) { row = 0; ++depth; }
			if (depth >= mat_size) { depth = 0; ++warp; }
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
	const size_t mat_size_2; // mat_size^2
	const size_t mat_size_3; // mat_size^3

	void Crypt() {


		std::vector<size_t> pass = hexToIntVector(this->password.hash);
		std::vector<size_t> pub = hexToIntVector(this->pubkey.hash);

		size_t const passSize = pass.size(), pubSize = pub.size();

		MultiArray<size_t> temp(mat_size);


		for (size_t row = 0, column = 0, depth = 0, warp = 0, keypos = 0; warp < mat_size;) {
			size_t index = (column + row * mat_size) + (depth * mat_size_2) + (warp * mat_size_3);
			std::string& cell = arr.array.get()[index];
			size_t& tempcell = temp.array.get()[index];
			const size_t intcell = hexToInt(cell);
			const size_t& passval = pass[keypos];
			const size_t& pubval = pub[keypos];
			tempcell = ((cell != "") ? (((row % 2 == 0 && depth % 2 == 1) || (row % 2 == 1 && depth % 2 == 0)) ? passval * intcell : pubval * intcell) : ((row % 2 == 0) ? passval * TRASH : pubval * TRASH));
			++column, ++keypos;
			if (column >= mat_size) { column = 0; ++row; }
			if (row >= mat_size) { row = 0; ++depth; }
			if (depth >= mat_size) { depth = 0; ++warp; }
			if (keypos >= pass.size()) { keypos = 0; }
		}

		this->Shift<size_t>(temp, pass[mat_size % passSize], true);

		for (size_t row = 0, column = 0, depth = 0, warp = 0, keypos = 0; warp < mat_size;) {
			size_t index = (column + row * mat_size) + (depth * mat_size_2) + (warp * mat_size_3);
			std::string& cell = arr.array.get()[index];
			const size_t& passval = pass[keypos];
			const size_t& pubval = pub[keypos];
			size_t const intcell = temp.array.get()[index];
			cell = ((((column % 2 == 0 && depth % 2 == 1) || (column % 2 == 1 && depth % 2 == 0)) ? intToHex(passval * intcell) : intToHex(pubval * intcell)));
			++column, ++keypos;
			if (column >= mat_size) { column = 0; ++row; }
			if (row >= mat_size) { row = 0; ++depth; }
			if (depth >= mat_size) { depth = 0; ++warp; }
			if (keypos >= pass.size()) { keypos = 0; }
		}
		this->Shift<std::string>(arr, pub[mat_size % pubSize]);
	}

	void undoCrypt() {
		std::vector<size_t> pass = hexToIntVector(this->password.hash);
		std::vector<size_t> pub = hexToIntVector(this->pubkey.hash);

		const size_t passSize = pass.size(), pubSize = pub.size();

		MultiArray<size_t> temp(mat_size);

		this->Shift<std::string>(arr, pub[mat_size % pubSize], true);

		for (size_t row = 0, column = 0, depth = 0, warp = 0, keypos = 0; warp < mat_size;) {
			size_t index = (column + row * mat_size) + (depth * mat_size_2) + (warp * mat_size_3);
			const size_t& passval = pass[keypos];
			const size_t& pubval = pub[keypos];
			const std::string& cell = arr.array.get()[index];
			size_t& tempcell = temp.array.get()[index];
			const size_t intcell = hexToInt(cell);
			tempcell = (((column % 2 == 0 && depth % 2 == 1) || (column % 2 == 1 && depth % 2 == 0)) ? intcell / (passval) : intcell / (pubval));
			++column, ++keypos;
			if (column >= mat_size) { column = 0; ++row; }
			if (row >= mat_size) { row = 0; ++depth; }
			if (depth >= mat_size) { depth = 0; ++warp; }
			if (keypos >= pass.size()) { keypos = 0; }
		}

		this->Shift<size_t>(temp, pass[mat_size % passSize]);

		for (size_t row = 0, column = 0, depth = 0, warp = 0, keypos = 0; warp < mat_size;) {
			size_t index = (column + row * mat_size) + (depth * mat_size_2) + (warp * mat_size_3);
			std::string& cell = arr.array.get()[index];
			const size_t& intcell = temp.array.get()[index];
			const size_t& passval = pass[keypos];
			const size_t& pubval = pub[keypos];
			cell = ((intcell / (pubval) != TRASH && intcell / (passval) != TRASH) ? (((row % 2 == 0 && depth % 2 == 1) || (row % 2 == 1 && depth % 2 == 0)) ? intToHex(intcell / (passval)) : intToHex(intcell / (pubval))) : "");
			++column, ++keypos;
			if (column >= mat_size) { column = 0; ++row; }
			if (row >= mat_size) { row = 0; ++depth; }
			if (depth >= mat_size) { depth = 0; ++warp; }
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
	d.Parse(ss.str().c_str());
	//Cycle through all config settings
	for (auto& configval : config) {
		const char* key = configval.first;

		if (d.FindMember(key) == d.MemberEnd()) { //If value doesn't exist in JSON file, warn the user and use default value if they choose
			Colors::TextColor(YELLOW, BLACK);
			std::cout << "\n\nWARNING: Value \"" << key << "\" could not be found in settings.json."
				<< "\nContinuing will keep this value at its default setting.\n\nDo you accept the risks?"
				<< "\n\nPress ENTER to continue, or CTRL+C to exit.";
			std::cin.ignore(LLONG_MAX, '\n');
			Colors::TextColor(WHITE, BLACK);
			continue;
		}

		rapidjson::Value& value = d[key];

		if (value.IsBool()) {
			setConfigVal<bool>(key, value.GetBool());
		}
		else if (value.IsInt()) {
			setConfigVal<unsigned int>(key, value.GetInt());
		}
		else if (value.IsString()) {
			setConfigVal<const char*>(key, value.GetString());
		}
	}
	chunkSize = getConfigVal<int>("chunkLimit") * 1000000;
	TRASH = getConfigVal<int>("noiseSeed");
	giveSSDWarning = getConfigVal<bool>("giveSSDWarning");
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


std::string encrypt(path file) {
	bool fileIsChunk = (file.string().find(".part") != std::string::npos);
	if (fileIsChunk) setConfigVal<bool>("useCompression", false); // If the file is a chunk file, do not pre-compress

	bit7z::Bit7zLibrary lib;
	bit7z::BitCompressor compressor(lib, bit7z::BitFormat::SevenZip);
	compressor.setCompressionLevel(static_cast<bit7z::BitCompressionLevel>(getConfigVal<int>("compressionLevel")));
	std::vector<std::wstring> files = { file.wstring() };
	if (getConfigVal<bool>("useCompression")) {
		compressor.compress(files, file.wstring() + L".kc");
		file += L".kc"; // Add .kc extension if pre-compressing
	}

	std::unique_lock<std::mutex> lock(kvmutex);
	std::string seed;

	if (!privateKey.Initialized()) {
			std::cout << "\n\nInput a seed to generate your private key OR press ENTER to use an existing key: ";
			std::getline(std::cin, seed);
			if (seed.empty()) {
				if (getConfigVal<const char*>("privateKey") != "") {
					Colors::TextColor(CYAN, BLACK);
					std::cout << "\n\nDetected a pre-set private key in settings.json. Using that as private key.";
					Colors::TextColor(WHITE, BLACK);
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
		std::cout << "\n\nInput a password to lock the file(s) with: ";
		std::getline(std::cin, seed);
		passwordHash = Key(seed, false);
	}
	if (!publicKey.Initialized()) {
		publicKey = Crypto::makePublic(privateKey, passwordHash, allowedCrypts);
	}

	lock.unlock();

	std::cout << "\n\nReading file....";
	std::stringstream ss;
	readFile(file, ss);
	std::string input = ss.str();
	ss.str("");

	if (getConfigVal<bool>("useCompression"))
		FileShredder::ShredFile(file, giveSSDWarning);

	Matrix m(input.size());
	m.Init(input);
	m.InitKeys(publicKey, passwordHash);
	std::cout << "\n\nPerforming encryption....";
	m.Shuffle();
	std::cout << "\n\nWriting encrypted data to output....";

	if (!getConfigVal<bool>("useCompression")) file += ".kc"; // Add .kc extension if pre-compression wasn't enabled, otherwise file already has it

	m.UnravelAndWrite(file, true);

	files = { file.wstring() }; // Save path of .kc file

	file.replace_extension(".kv2"); // Switch final extension to .kv2 before final compression

	if (exists(file.wstring())) FileShredder::ShredFile(file.wstring(), giveSSDWarning); // If a .kv2 file with the same name already exists, destroy it first

	compressor.setPassword(path(Key(publicKey.hash, false).hash).wstring());
	compressor.compress(files, file.wstring());

	addMarker(file.string()); // Add kv2 identifier marker to end of file

	FileShredder::ShredFile(files[0], giveSSDWarning); // Destroy .kc file

	return file.string(); // Return path of .kv2 file
}

std::string decrypt(path file, bool mult) {
	bool isCompressed = true;
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
	extractor.extract(file.wstring(), file.parent_path()); // Extract .kc file from .kv2 file

	//Extension may or may not be present, check for it
	if (file.extension() == ".kv2")
		file.replace_extension(".kc"); // If .kv2 extension is present, replace it with .kc
	else
		file += ".kc"; // If no .kv2 extension is present, append .kc extension

	std::cout << "\n\nReading encrypted file....";
	getContent(file.string(), counts, input); // Read and decrypt inner .kc file
	FileShredder::ShredFile(file, giveSSDWarning); // Destroy inner .kc file after reading
	Matrix m(counts.size());
	m.InitFromFile(input, counts);
	m.InitKeys(publicKey, passwordHash);
	std::cout << "\n\nPerforming decryption....";
	m.DeShuffle();
	m.FillSpaces();
	std::cout << "\n\nWriting decrypted data to output....";

	m.UnravelAndWrite(file, false); // Write decrypted data to new .kc file

	char* buffer = new char[4];
	reader.open(file.string(), std::ifstream::binary);
	reader.get(buffer, 4);
	reader.close();

	if (std::string(buffer) != "7z¼") isCompressed = false; // Check if decrypted data is compressed by checking for 7zip header

	delete[] buffer;

    if (isCompressed) { // If file was pre-compressed, extract raw file out of decrypted .kc
        extractor.clearPassword();
        extractor.extract(file.wstring(), file.parent_path());
        FileShredder::ShredFile(file, giveSSDWarning);
    }
    else // Elsewise, simply remove the .kc extension from the decrypted .kc
    {
        rename(file, path(file).replace_extension(""));
    }
	return file.replace_extension("").string(); // Return filepath with .kc removed
}

void encryptFromList(std::vector<path>& filelist) {

	std::vector<path> paths;

	for (size_t listindex = 0; listindex < filelist.size(); ++listindex) {
		path& file = filelist[listindex];
		if (is_directory(file)) {
			for (std::filesystem::directory_entry p : recursive_directory_iterator(file)) {
				if (!is_directory(p)) {
					paths.push_back(p.path().string());
				}
			}
		}
		else paths.push_back(file);
	}

	std::string input;

	std::atomic<int> pos = 0, internal_counter = 0;

	unsigned int hConcurrence = std::thread::hardware_concurrency();

	const unsigned int max_threads = (hConcurrence ? hConcurrence : 1);

	std::vector<path> chunkedpaths;
	std::vector<std::thread> threads;
	std::vector<path> oldnames;

	bool keepScramble = (getConfigVal<bool>("scrambleName") && !getConfigVal<bool>("deleteOriginals"));

	for (path& file : paths) {

		if (getConfigVal<bool>("scrambleName") && !isKV2File(file)) {

			if (keepScramble)
				oldnames.push_back(file);

			std::string name;
			path basepath = path(file);
			path newpath;
			do {
				newpath = path(file);
				name = static_cast<path>(std::tmpnam(nullptr)).filename().string();
			} while (exists(newpath.replace_filename(name)));
			rename(basepath, newpath);

			file = newpath.string();
		}

		std::ifstream in(file, std::ifstream::ate | std::ifstream::binary);
		if (!isKV2File(file) && (size_t)in.tellg() > chunkSize) {
			in.close();
			for (path p : chunkFile(file))
				chunkedpaths.push_back(p.string());

			if (keepScramble) { //Return files back to original name after splitting is done, if they are preserved and scrambled
				const int res = rename(file.string().c_str(), oldnames.back().string().c_str());
				if (res != 0) {
					Colors::TextColor(RED, BLACK);
					std::cout << "\n\nFAILED TO RENAME FILE AT \"" << oldnames.back() << "\", ORIGINAL FILE NAMES MAY NOT BE PRESERVED.";
					Colors::TextColor(WHITE, BLACK);
				}
				oldnames.pop_back();
			}

		}
		else chunkedpaths.push_back(file);
	}

	std::sort(chunkedpaths.begin(), chunkedpaths.end());
	std::sort(oldnames.begin(), oldnames.end());

	for (auto file : chunkedpaths) {

		bool showKeys = false;

		threads.push_back(std::thread([file, chunkedpaths, &showKeys, &pos, &keepScramble, &oldnames, &internal_counter] {

			try {

				bool enc = false;

				std::string pa;
				if (!isKV2File(file)) {

					pa = encrypt(file);

					if (keepScramble && file.string().find(".part") == std::string::npos) { //Rename original file to its original name if it was scrambled for encryption but keepScramble is enabled
						const int res = rename(file.string().c_str(), oldnames[internal_counter].string().c_str());
						if (res != 0) {
							Colors::TextColor(RED, BLACK);
							std::cout << "\n\nFAILED TO RENAME FILE AT \"" << oldnames[internal_counter] << "\", ORIGINAL FILE NAMES MAY NOT BE PRESERVED.";
							Colors::TextColor(WHITE, BLACK);
						}
					}

					if (!showKeys) showKeys = true;
					enc = true;
				}
				else {
					pa = decrypt(file, chunkedpaths.size() != 1);
				}
				if (getConfigVal<bool>("deleteOriginals") || (path(file).string().find(".part") != std::string::npos && !isKV2File(file)))
					FileShredder::ShredFile(file, giveSSDWarning);

				Colors::TextColor(GREEN, BLACK);
				std::cout << "\n\nSuccessfully ";
				Colors::TextColor(CYAN, BLACK);
				std::cout << ((enc) ? std::string("encrypted") : std::string("decrypted"));
				Colors::TextColor(GREEN, BLACK);
				std::cout << " file " << std::to_string(++pos) << "/" << std::to_string(chunkedpaths.size()) << " to " << pa;
				Colors::TextColor(WHITE, BLACK);
			}
			catch (std::exception e) {
				Colors::TextColor(RED, BLACK);
				std::cout << "\n\n" << e.what();
				waitForExit();
				return 1;
			}
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
			Colors::TextColor(GREEN, BLACK);
			std::cout << "\n\nFINISHED!";
			Colors::TextColor(WHITE, BLACK);
			if (showKeys) {
				std::cout << "\n\nYour public key is: ";
				Colors::TextColor(CYAN, BLACK);
				std::cout << publicKey.hash;
				Colors::TextColor(WHITE, BLACK);
				std::cout << "\nThis key, along with the password, will be needed to decrypt the file(s).";
				std::cout << "\n\nYour private key is: ";
				Colors::TextColor(MAGENTA, BLACK);
				std::cout << privateKey.hash;
				Colors::TextColor(RED, BLACK);
				std::cout << "\nKEEP THIS SOMEWHERE SAFE. ";
				Colors::TextColor(WHITE, BLACK);
				std::cout << "You will only be able to generate another public key by knowing this and the password.";
			}
			waitForExit();
		}
		++internal_counter;
	}
}

bool verifyFilesExist(std::vector<path> filelist) {
	for (path file : filelist) {
		if (!exists(file)) {
			throw std::exception(std::string("\nFILEPATH \"" + file.string() + "\" DOES NOT EXIST.").c_str());
			return false;
		}
	}
	return true;
}

void recoverPublicKey() {
	std::string stringinput;
	Colors::TextColor(YELLOW, BLACK);
	std::cout << "\n\nNOTE: Key recovery will only work if using the same encryption method!";
	Colors::TextColor(WHITE, BLACK);
	std::cout << "\n\nInput the private key that was generated: ";
	std::getline(std::cin, stringinput);
	Key priv;
	priv.Initialize(stringinput);
	std::cout << "\n\nInput the password that was used: ";
	std::getline(std::cin, stringinput);
	Key pass(stringinput, false);
	std::cout << "\n\nRecovering public key...";
	Key pub = Crypto::makePublic(priv, pass, allowedCrypts);
	std::cout << "\n\nPublic key is: ";
	Colors::TextColor(CYAN, BLACK);
	std::cout << pub.hash;
	Colors::TextColor(WHITE, BLACK);
	waitForExit();
}

void encryptString() {
	std::string stringinput;
	std::string file;
	std::cout << "\n\nEnter text to encrypt: ";
	std::getline(std::cin, stringinput);
	std::cout << "\n\nSet output file path: ";
	std::getline(std::cin, file);
	std::stringstream ss(stringinput);
	writeFile(file, ss);
	encrypt(file);
	if (getConfigVal<bool>("deleteOriginals"))
		FileShredder::ShredFile(file, giveSSDWarning);
	Colors::TextColor(GREEN, BLACK);
	std::cout << "\n\nFINISHED.";
	Colors::TextColor(WHITE, BLACK);
	std::cout << "\n\nYour public key is: ";
	Colors::TextColor(CYAN, BLACK);
	std::cout << publicKey.hash;
	Colors::TextColor(WHITE, BLACK);
	std::cout << "\nThis key, along with the password, will be needed to decrypt the file(s).";
	std::cout << "\n\nYour private key is: ";
	Colors::TextColor(MAGENTA, BLACK);
	std::cout << privateKey.hash;
	Colors::TextColor(RED, BLACK);
	std::cout << "\nKEEP THIS SOMEWHERE SAFE. ";
	Colors::TextColor(WHITE, BLACK);
	std::cout << "You will only be able to generate another public key by knowing this and the password.";
	waitForExit();
}

void showUsage(const char* paramName) {
	std::cout << "No parameters given for parameter " << paramName << ". Usage is as follows:\n\n"
		<< "Kryptr V2 -Paths <FILEPATH_1 [FILEPATH_...]> -Password <password> [-Seed <seed>] [-publicKey <public key>] [-useCompression] [-useFullPower] [-deleteOriginals] "
		<< "[-scrambleName] [-compressionLevel <LOWEST = 1, LOW = 3, NORMAL = 5, HIGH = 7, MAX = 9>] [-chunkLimit <size in MB>] "
		<< "[-noiseSeed <seed>] [-privateKey <key>] [-IOBufferSize <size in bytes>]";
	waitForExit();
}

bool parseFlag(const char* flagName, int& argc, char** argv) {
	for (int i = 0; i < argc; ++i) {
		if (argv[i] == "-" + std::string(flagName)) {
			return true;
		}
		else {
			return false;
		}
	}
}

template<typename T = const char*>
std::vector<T> parseArgument(const char* paramName, int& argc, char** argv) {
	std::vector<T> out;
	for (int i = 0; i < argc; ++i) {
		if (argv[i] == "-" + std::string(paramName)) {
			if (i + 1 >= argc || argv[i + 1][0] == '-') { //If the arguments cut off or the next spot is another parameter, show correct usage of argument
				showUsage(paramName);
			}
			else {
				while (i + 1 < argc && argv[i + 1][0] != '-') { //Until the argument cuts off, parse all values
					out.push_back(T(argv[++i]));
				}
				return out;
			}
		}
	}
	return out;
}


int main(int argc, char** argv)
{
	try {
		std::vector<path> filelist;

		//We want our streams to be speedy, thanks
		std::ios_base::sync_with_stdio(false);

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
			passwordHash = Key(argv[4], false);
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

			filelist = split<path>(inputs, ",");
			verifyFilesExist(filelist);
			encryptFromList(filelist);

		}
		else if (argv[1] != nullptr && argv[1] == std::string("RECOVERYHOOK")) {
			recoverPublicKey();
		}
		else if (argv[1] != nullptr && argv[1] == std::string("STRINGHOOK")) {
			encryptString();
		}
		//Handle file association for Windows
		else if (argc == 2) {
			filelist.push_back(argv[1]);
			verifyFilesExist(filelist);
			encryptFromList(filelist);
		}
		else {
			/*
			if (argc == 1) {
				std::cout << "No parameters given. Usage is as follows:\n\n"
					<< "Kryptr V2 -Paths <FILEPATH_1 [FILEPATH_...]> [-Password <password>] [-Seed <seed>] [-publicKey <public key>] [-useCompression] [-useFullPower] [-deleteOriginals] "
					<< "[-scrambleName] [-compressionLevel <LOWEST = 1, LOW = 3, NORMAL = 5, HIGH = 7, MAX = 9>] [-chunkLimit <size in MB>] "
					<< "[-noiseSeed <seed>] [-privateKey <key>] [-IOBufferSize <size in bytes>]";
				waitForExit();
			}
			filelist = parseArgument<path>("Paths", argc, argv);
			verifyFilesExist(filelist);
			encryptFromList(filelist);
			*/
		}

		return 0;
	}
	catch (std::exception e) {
		Colors::TextColor(RED, BLACK);
		std::cout << "\n\n" << e.what();
		waitForExit();
		return 1;
	}
}