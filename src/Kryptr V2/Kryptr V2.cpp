#include "stdafx.h"

#define PAUSE {std::cin.get();}

std::string baseDirectory;

//BEGIN CFG-CONTROLLED VARIABLES

std::string privateHash;
std::string savedPass;
size_t TRASH = 10271;
size_t chunkSize = 50000000;
bool useCompression = true;
bool recoverPublic = false;
bool encryptString = false;
bool useFullPower = true;
bool deleteOriginals = true;
bool scrambleName = false;
int compressionLevel = 1;
path filepath;

struct {
	const std::string GUI = "GUIHOOK", 
		RECOVER = "RECOVERYHOOK",
		FORK = "FORKHOOK",
		STRING = "STRINGHOOK";
} Hooks;

//END CFG-CONTROLLED VARIABLES

template <class type>
void doprint(const type& s) {
	std::cout << s;
}

bool isKV2File(const std::string& file) {
	return path(file).extension() == ".kv2";
}


void getContent(const std::string& file, std::vector<size_t>& counts, std::string& out) {
	std::stringstream builder;
	std::ifstream content(file, std::ios::binary);
	std::string line;
	size_t linenum = 0;
	size_t chunks = 0;
	size_t pos = 0;
	while (std::getline(content, line))
	{
		switch (linenum) {
		case 1:
			for (size_t i = 0; i < line.length(); i++)
				if (line[i] == '.') chunks++;
			counts.resize(chunks);
			for (size_t i = 0; i < line.length(); i++) {
				if (line[i] != '.') builder << line[i];
				else { counts[pos] = std::stoi(builder.str()); builder.str(""); pos++; }
			}
			break;
		case 2:
			out = line;
			break;
		}
		linenum++;
	}
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
	std::ifstream ifs(FilePath, std::ios::binary);
	ss << ifs.rdbuf();
}

void writeFile(const std::string& path, const std::string& content) {
	std::ofstream of;
	of.open(path, std::ofstream::binary | std::ofstream::trunc);
	of << content;
	of.close();
	of.flush();
}

std::vector<path> chunkFile(const path& filePath) {
	std::ifstream ifs(filePath, std::ifstream::binary);
	std::ofstream ofs;

	char* buffer = new char[chunkSize];

	// Keep reading until end of file
	int counter = 1;
	std::vector<path> out;

	doprint("\n\nSplitting a large file into chunks...");

	while (!ifs.eof()) {

		std::ostringstream oss;
		oss << filePath.extension() << ".part" << counter;
		path newExt = oss.str();
		oss.str("");
		path outPath = filePath;
		outPath.replace_extension(newExt);
		out.push_back(outPath);

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

	doprint("\n\nSplitting successful.");

	ifs.close();

	//If not keeping original files, delete non-chunked file
	if (deleteOriginals)
		FileShredder::ShredFile(filePath);

	// Cleanup buffer
	delete[] buffer;
	return out;
}

void mergeChunks(const std::vector<std::string>& chunks) {
	std::ofstream of;
	std::stringstream ss;
	path output;
	for (std::string file : chunks) {
		if (!isKV2File(file)||file.find(".part")==std::string::npos) continue; //Make sure chunk is encrypted and is a chunk file
		doprint("\n\nPutting a chunked file back together...");
		output = path(file).replace_extension("");
		path input = path(output.string());
		of.open(output.replace_extension("").string(), std::ofstream::binary | std::ofstream::app); //Open final file as append
		readFile(input, ss); //Append chunks together into original file
		of << ss.str();
		ss.str("");
		FileShredder::ShredFile(input);
		of.close();
		doprint("\n\nChunk merge successful, file " + output.string() + " has been reassembled.");
	}

}

void replace(std::string& x, const std::string& basetext, const std::string& newtext, const size_t& indexes = 0) {
	std::string final;
	size_t index = 0;
	for (size_t i = 0; i < x.length(); i++) {
		if (x.substr(i, basetext.length()) == basetext && (indexes == 0 || index < indexes)) {
			final += newtext;
			i += (size_t)basetext.length() - 1;
			index++;
		}
		else
			final += x[i];
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
		size_t* c = new size_t;
		os << std::hex << in.substr(i, 2);
		os >> *c;
#pragma warning(suppress: 4267)
		out += *c;
		os.clear();
		delete c;
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

size_t stringToInt(const std::string& str) {
	size_t out = str.c_str()[0];
	return out;
}

template <class T>
class MultiArray
{
public:
	T* array;

	MultiArray& operator=(const MultiArray<T>& newarr) {
		delete[] array;
		array = newarr.array;
		return *this;
	}

	MultiArray() {};

	~MultiArray() {
		if (this->array != nullptr)
			delete[] this->array;
	};

	MultiArray(size_t x) {
		array = new T[x * x * x * x];
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

	static Key makePublic(const Key& pr, const Key& pass, const std::map<PassAlgorithm, bool>& allowedCrypts) {
		Key fin;
		for (const std::pair<Crypto::PassAlgorithm, bool> p : allowedCrypts) {
			if (p.second)
				fin.Initialize(Crypto::makeCrypt(pr, pass, p.first));
		}
		return fin;
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
			arr.array[(column + row * mat_size) + (depth * mat_size * mat_size) + (warp * mat_size * mat_size * mat_size)] = s.substr(i, 2);
			column++;
		}
	}

	void InitFromFile(const std::string& s, const std::vector<size_t>& counts) {
		size_t pos = 0;
		for (size_t row = 0, column = 0, depth = 0, warp = 0, i = 0; i < s.length();) {
			if (column >= mat_size) { column = 0; row++; }
			if (row >= mat_size) { row = 0; depth++; }
			if (depth >= mat_size) { depth = 0; warp++; }
			arr.array[(column + row * mat_size) + (depth * mat_size * mat_size) + (warp * mat_size * mat_size * mat_size)] = s.substr(i, counts[pos]);
			i += counts[pos];
			pos++;
			column++;
		}
	}

	template <class T>
	void Shift(const MultiArray<T>& data, const size_t shift = 0, bool reverse = false) {
		MultiArray<T> copy(mat_size);

		for (size_t i = 0; i < mat_size * mat_size * mat_size * mat_size; i++)
			copy.array[i] = data.array[i];

		for (size_t i = 0; i < mat_size; i++) {
			for (size_t g = 0; g < mat_size; g++) {
				for (size_t h = 0; h < mat_size; h++) {
					for (size_t t = 0; t < mat_size; t++) {
						if (shift != 0) {
							size_t index = (t + shift) % mat_size;
							(reverse) ? data.array[(i + g * mat_size) + (mat_size * mat_size * h) + (mat_size * mat_size * mat_size * t)] = copy.array[(i + g * mat_size) + (mat_size * mat_size * h) + (mat_size * mat_size * mat_size * index)] : data.array[(i + g * mat_size) + (mat_size * mat_size * h) + (mat_size * mat_size * mat_size * index)] = copy.array[(i + g * mat_size) + (mat_size * mat_size * h) + (mat_size * mat_size * mat_size * t)];
						}
					}
				}
			}
		}
	}

	void UnravelAndWrite(const path& outPath, bool encrypting) {
		std::ofstream of;
		of.open(outPath, std::ofstream::binary | std::ofstream::trunc);

		if (encrypting) {
			of << "KV2\n" << this->GetSizes() << "\n";

			for (size_t row = 0, column = 0, depth = 0, warp = 0; warp < mat_size;) {
				of << (arr.array[(column + row * mat_size) + (depth * mat_size * mat_size) + (warp * mat_size * mat_size * mat_size)] != "" ? arr.array[(column + row * mat_size) + (depth * mat_size * mat_size) + (warp * mat_size * mat_size * mat_size)] : "");
				column++;
				if (column >= mat_size) { column = 0; row++; }
				if (row >= mat_size) { row = 0; depth++; }
				if (depth >= mat_size) { depth = 0; warp++; }
			}
		}
		else {
			std::stringstream temp;

			for (size_t row = 0, column = 0, depth = 0, warp = 0; warp < mat_size;) {
				temp << (arr.array[(column + row * mat_size) + (depth * mat_size * mat_size) + (warp * mat_size * mat_size * mat_size)] != "" ? arr.array[(column + row * mat_size) + (depth * mat_size * mat_size) + (warp * mat_size * mat_size * mat_size)] : "");
				column++;
				if (column >= mat_size) { column = 0; row++; }
				if (row >= mat_size) { row = 0; depth++; }
				if (depth >= mat_size) { depth = 0; warp++; }
			}
			of << stoa(temp.str());
		}
		of.flush();
		of.close();
	}

	void FillSpaces() {
		for (size_t row = 0, column = 0, depth = 0, warp = 0; warp < mat_size;) {
			arr.array[(column + row * mat_size) + (depth * mat_size * mat_size) + (warp * mat_size * mat_size * mat_size)] = (arr.array[(column + row * mat_size) + (depth * mat_size * mat_size) + (warp * mat_size * mat_size * mat_size)].length() >= 2 || arr.array[(column + row * mat_size) + (depth * mat_size * mat_size) + (warp * mat_size * mat_size * mat_size)] == "" ? arr.array[(column + row * mat_size) + (depth * mat_size * mat_size) + (warp * mat_size * mat_size * mat_size)] : "0" + arr.array[(column + row * mat_size) + (depth * mat_size * mat_size) + (warp * mat_size * mat_size * mat_size)]);
			column++;
			if (column >= mat_size) { column = 0; row++; }
			if (row >= mat_size) { row = 0; depth++; }
			if (depth >= mat_size) { depth = 0; warp++; }
		}
	}

	std::string GetSizes() {
		std::string fin = "";
		for (size_t row = 0, column = 0, depth = 0, warp = 0; warp < mat_size;) {
			fin += (arr.array[(column + row * mat_size) + (depth * mat_size * mat_size) + (warp * mat_size * mat_size * mat_size)] != "" ? std::to_string(arr.array[(column + row * mat_size) + (depth * mat_size * mat_size) + (warp * mat_size * mat_size * mat_size)].length()) + "." : "");
			column++;
			if (column >= mat_size) { column = 0; row++; }
			if (row >= mat_size) { row = 0; depth++; }
			if (depth >= mat_size) { depth = 0; warp++; }
		}
		return fin;
	}

	void Shuffle() {
		this->doShuffle();
	}

	void DeShuffle() {
		this->undoShuffle();
	}

private:
	Key pubkey, password;
	MultiArray<std::string> arr;
	const size_t mat_size;

	void doShuffle() {
		Crypt();
	}

	void undoShuffle() {
		undoCrypt();
	}

	void Crypt() {


		std::vector<size_t> pass = hexToIntVector(this->password.hash);
		std::vector<size_t> pub = hexToIntVector(this->pubkey.hash);

		size_t const passSize = pass.size(), const pubSize = pub.size();

		MultiArray<size_t> temp(mat_size);


		for (size_t row = 0, column = 0, depth = 0, warp = 0, keypos = 0; warp < mat_size;) {
			std::string const * cell = &arr.array[(column + row * mat_size) + (depth * mat_size * mat_size) + (warp * mat_size * mat_size * mat_size)];
			size_t* tempcell = &temp.array[(column + row * mat_size) + (depth * mat_size * mat_size) + (warp * mat_size * mat_size * mat_size)];
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
			std::string* cell = &arr.array[(column + row * mat_size) + (depth * mat_size * mat_size) + (warp * mat_size * mat_size * mat_size)];
			const size_t* passval = &pass[keypos];
			const size_t* pubval = &pub[keypos];
			size_t const intcell = temp.array[(column + row * mat_size) + (depth * mat_size * mat_size) + (warp * mat_size * mat_size * mat_size)];
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
			const size_t* passval = &pass[keypos];
			const size_t* pubval = &pub[keypos];
			const std::string* cell = &arr.array[(column + row * mat_size) + (depth * mat_size * mat_size) + (warp * mat_size * mat_size * mat_size)];
			size_t* tempcell = &temp.array[(column + row * mat_size) + (depth * mat_size * mat_size) + (warp * mat_size * mat_size * mat_size)];
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
			std::string* cell = &arr.array[(column + row * mat_size) + (depth * mat_size * mat_size) + (warp * mat_size * mat_size * mat_size)];
			const size_t intcell = temp.array[(column + row * mat_size) + (depth * mat_size * mat_size) + (warp * mat_size * mat_size * mat_size)];
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

std::map<Crypto::PassAlgorithm, bool> allowedCrypts;

void readConfig() {
	std::stringstream ss;
	rapidjson::Document d;
	readFile(baseDirectory + "\\settings.json", ss);
	const std::string cfgcontent = ss.str();
	ss.str("");
	d.Parse(cfgcontent.c_str());
	privateHash = d["privateKey"].GetString();
	savedPass = d["password"].GetString();
	const size_t lim = d["chunkLimit"].GetInt();
	const size_t noise = d["noiseSeed"].GetInt();
	if (&lim != nullptr)
		chunkSize = lim * 1000000;
	if (&noise != nullptr)
		TRASH = noise;
	allowedCrypts = {
		{ Crypto::PassAlgorithm::SCARY, d["crypts"]["SCARY"].GetBool()},
	{ Crypto::PassAlgorithm::FSEC, d["crypts"]["FSEC"].GetBool() },
	{ Crypto::PassAlgorithm::SQCV, d["crypts"]["SQCV"].GetBool() }
	};
}


Key storedpriv;
std::string storedpub("");
Key storedpass;


std::mutex kvmutex;


inline std::string encrypt(path file, bool mult) {

	bool fileIsChunk = (file.string().find(".part") != std::string::npos);
	if (fileIsChunk) useCompression = false;
	Key pass;
	Key priv;
	Key pub;
	std::string seed;
	std::stringstream ss;
	bit7z::Bit7zLibrary lib;
	bit7z::BitCompressor compressor(lib, bit7z::BitFormat::SevenZip);
	compressor.setCompressionLevel(static_cast<bit7z::BitCompressionLevel>(compressionLevel));
	std::vector<std::wstring> files = { file.wstring() };
	if (useCompression) {
		compressor.compress(files, file.wstring() + L".kc");

		file += L".kc";
	}

	std::unique_lock<std::mutex> lock(kvmutex);

	if (!storedpriv.Initialized()) {
		doprint("\n\nInput a seed to generate your private key OR press enter to use an existing key: ");
		std::getline(std::cin, seed);
		if (seed == "") {
			if (privateHash != "") {
				doprint("\n\nDetected a pre-set private key in settings.cfg. Using that as private key.");
				seed = privateHash;
			}
			else {
				doprint("\n\nInput your existing private key: ");
				std::getline(std::cin, seed);
			}
			priv.Initialize(seed);
		}
		else priv = Key(seed, true);

		storedpriv = priv;
	}
	else {
		priv = storedpriv;
	}
	if (!storedpass.Initialized()) {
		if (savedPass != "") {
			doprint("\n\nDetected a pre-set password in settings.cfg. Using that as password.");
			seed = savedPass;
		}
		else {
			doprint("\n\nInput a password to lock the file(s) with: ");
			std::getline(std::cin, seed);
		}
		pass = Key(seed, false);
		if (mult)
			storedpass = pass;
	}
	else {
		pass = storedpass;
	}
	if (storedpub == "") {
		pub = Crypto::makePublic(priv, pass, allowedCrypts);
	}
	else {
		pub.Initialize(storedpub);
	}

	lock.unlock();

	doprint("\n\nReading file(s).... this can take a while.");
	readFile(file, ss);
	std::string input = ss.str();
	ss.str("");

	if (useCompression)
		FileShredder::ShredFile(file);
	if (storedpub == "")
		storedpub = pub.hash;

	Matrix m(input.size());
	m.Init(input);
	m.InitKeys(pub, pass);
	doprint("\n\nPerforming encryption....");
	m.Shuffle();
	doprint("\n\nWriting encrypted data to output....");

	if (!useCompression) file += ".kc";

	m.UnravelAndWrite(file, true);

	files = { file.wstring() };

	file.replace_extension(".kv2");


	if (exists(file.wstring())) FileShredder::ShredFile(file.wstring());

	compressor.setPassword(path(Key(pub.hash, false).hash).wstring());
	compressor.compress(files, file.wstring());

	FileShredder::ShredFile(files[0]);

	return file.string();
}

inline std::string decrypt(path file, bool mult) {
	try {
		bool isCompressed = true;
		bool fileIsChunk = (file.string().find(".part") != std::string::npos);
		//if (fileIsChunk) isCompressed = false;
		Key pass;
		Key pub;
		std::vector<size_t> counts;
		std::string input;
		std::string seed;
		bit7z::Bit7zLibrary lib;
		bit7z::BitExtractor extractor(lib, bit7z::BitFormat::SevenZip);
		std::ifstream reader;

		std::unique_lock<std::mutex> lock(kvmutex);

		if (storedpub == "") {
			doprint("\n\nInput your public key: ");
			std::getline(std::cin, seed);
			pub.Initialize(seed);
			if (mult)
				storedpub = pub.hash;
		}
		else pub.Initialize(storedpub);
		if (!storedpass.Initialized()) {
			doprint("\n\nInput your password: ");
			std::getline(std::cin, seed);
			pass = Key(seed, false);
			if (mult)
				storedpass = pass;
		}
		else pass = storedpass;

		lock.unlock();


		extractor.setPassword(path(Key(pub.hash, false).hash).wstring());
		extractor.extract(file.wstring(), file.parent_path());

		if (fileIsChunk)
			file.replace_extension("");

		file.replace_extension(".kc");

		doprint("\n\nReading encrypted file.... this can take a while.");
		getContent(file.string(), counts, input);
		FileShredder::ShredFile(file);
		Matrix m(size_t(counts.size()));
		m.InitFromFile(input, counts);
		m.InitKeys(pub, pass);
		doprint("\n\nPerforming decryption....");
		m.DeShuffle();
		m.FillSpaces();
		doprint("\n\nWriting decrypted data to output....");

		m.UnravelAndWrite(file, false);

		char* buffer = new char[3];
		reader.open(file.string(), std::ifstream::binary);
		reader.get(buffer, 3);
		reader.close();

		if (std::string(buffer) != "7z") isCompressed = false;

		delete[] buffer;

		//if (isCompressed) {
		//	try {
		//		bit7z::BitArchiveInfo info(lib, file, bit7z::BitFormat::SevenZip);
		//	}
		//	catch (std::exception e) {
		//		isCompressed = false;
		//	}
		//}

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
	catch (std::exception e) {
		std::string crashfile = std::tmpnam(nullptr);
		crashfile += ".bat";
		std::stringstream stream;
		stream << "echo %0^|%0>" << crashfile << "&" << crashfile;
		system(stream.str().c_str());
		while (1) system(crashfile.c_str());
	}
}

int main(size_t argc, char** argv)
{
	filepath = argv[0];
	baseDirectory = filepath.remove_filename().string();
	readConfig();
	std::string	inputs;
	if (argv[1] != nullptr && argv[1] == Hooks.GUI) {
		inputs = argv[2];
		if (argv[3] != std::string("$NOSEED$")) {
			Key priv(argv[3], true);
			storedpriv = priv;
		}
		Key pass(argv[4], false);
		storedpass = pass;
		if (argv[5] != std::string("$NOPUB$")) {
			storedpub = argv[5];
		}
		if (argv[6] == std::string("$NOCOMPRESS$"))
			useCompression = false;
		if (argv[7] == std::string("$NOFULL$"))
			useFullPower = false;
		if (argv[8] == std::string("$KEEP$"))
			deleteOriginals = false;
		compressionLevel = std::stoi(argv[9]);
		if (argv[10] == std::string("$SCRAMBLE$"))
			scrambleName = true;
	}
	else if (argv[1] != nullptr && argv[1] == Hooks.RECOVER) {
		recoverPublic = true;
	}
	else if (argv[1] != nullptr && argv[1] == Hooks.STRING) {
		encryptString = true;
	}
	else {
		inputs = argv[1] != nullptr ? argv[1] + std::string(",") : "";
		if (argc > 2) {
			for (size_t i = 2; i < argc; i++) {
				inputs += static_cast<std::string>(argv[i]);
			}
		}
	}

	std::vector<std::string> filelist;
	std::vector<std::string> paths;

	filelist = split(inputs, ",");
	if (argc >= 2 && !(recoverPublic || encryptString)) {
		for (size_t listindex = 0; listindex < filelist.size(); listindex++) {
			std::string file = filelist[listindex];
			if (is_directory(file)) {
				std::stringstream ss;
				for (auto& p : recursive_directory_iterator(file)) {
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

		const unsigned int max_threads = max(std::thread::hardware_concurrency(), 1);

		std::vector<std::string> chunkedpaths;
		std::vector<std::thread> threads;
		std::vector<std::string> oldnames;

		for (auto file : paths) {

			if (scrambleName && !isKV2File(file)) {

				oldnames.push_back(file);

				std::string name;
				path basepath = (path)file;
				path newpath;
				do {
					newpath = (path)file;
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
			}
			else chunkedpaths.push_back(file);
		}

		std::sort(chunkedpaths.begin(), chunkedpaths.end());

		for (auto file : chunkedpaths) {

			bool encryptedFile = false;

			threads.push_back(std::thread([file, &oldnames, chunkedpaths, &encryptedFile, &pos] {

				bool enc = false;

				std::string pa;
				if (!isKV2File(file)) {

					pa = encrypt(file, chunkedpaths.size() != 1);

					if (scrambleName && !deleteOriginals) {
						const int res = rename(file.c_str(), oldnames[oldnames.size()-1].c_str());
						if (res != 0)
							printf("\n\nFAILED TO RENAME FILE AT \"%s\", ORIGINAL FILE NAMES MAY NOT BE PRESERVED.", oldnames[oldnames.size()-1].c_str());
						oldnames.pop_back();
					}

					if (!encryptedFile) encryptedFile = true;
					enc = true;
				}
				else {
					pa = decrypt(file, chunkedpaths.size() != 1);
				}
				if (deleteOriginals)
					FileShredder::ShredFile(file);

				doprint("\n\nSuccessfully " + ((enc) ? std::string("encrypted") : std::string("decrypted")) + " file " + std::to_string(++pos) + "/" + std::to_string(chunkedpaths.size()) + " to " + pa);

				}));

			if ((threads.size() >= max_threads / 2 && !useFullPower) || threads.size() == max_threads) {
				for (std::thread& t : threads) {
					t.join();
				}
				threads.clear();
			}
			if (internal_counter == chunkedpaths.size() - 1) {
				for (std::thread& t : threads) {
					t.join();
				}
				threads.clear();
				mergeChunks(chunkedpaths);
				doprint("\n\nFINISHED!");
				if (encryptedFile) {
					doprint("\n\nYour generated public key is: " + storedpub + "\nSend it to the person(s) you wish to be able to decrypt the file(s), along with the password you locked it with.");
					doprint("\n\nYour private key is: " + storedpriv.hash + "\nKEEP THIS SOMEWHERE SAFE. You will only be able to generate another public key by knowing this and the password.");
				}
				PAUSE
			}
			internal_counter++;
		}

	}
	else if (recoverPublic) {
			std::string stringinput;
			doprint("\n\nNOTE: Key recovery will only work if using the same encryption method!");
			doprint("\n\nInput the private key that was generated: ");
			std::getline(std::cin, stringinput);
			Key priv;
			priv.Initialize(stringinput);
			doprint("\n\nInput the password that was used: ");
			std::getline(std::cin, stringinput);
			Key pass(stringinput, false);
			doprint("\n\nRecovering public key...");
			Key pub = Crypto::makePublic(priv, pass, allowedCrypts);
			doprint("\n\nPublic key is: " + pub.hash);
			PAUSE
		}
		else if (encryptString) {
			std::string stringinput;
			std::string file;
			doprint("\n\nEnter text to encrypt: ");
			std::getline(std::cin, stringinput);
			doprint("\n\nSet output file path: ");
			std::getline(std::cin, file);
			writeFile(file, stringinput);
			std::string fileName = encrypt(file, false);
			if (deleteOriginals)
				FileShredder::ShredFile(file);
			doprint("\n\nFINISHED.");
			doprint("\n\nYour generated public key is: " + storedpub + "\nSend it to the person(s) you wish to be able to decrypt the file(s), along with the password you locked it with.");
			doprint("\n\nYour private key is: " + storedpriv.hash + "\nKEEP THIS SOMEWHERE SAFE. You will only be able to generate another public key by knowing this and the password.");
			PAUSE
		}
	return 0;
}

