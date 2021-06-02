// Copyright (c) rAthena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "database.hpp"

#include "malloc.hpp"
#include "showmsg.hpp"

#include <iostream>

bool YamlDatabase::nodeExists( const ryml::NodeRef node, const std::string& name ){
	return node.has_child(c4::to_csubstr(name));
}

bool YamlDatabase::nodesExist( const ryml::NodeRef node, std::initializer_list<const std::string> names ){
	bool missing = false;

	for( const std::string& name : names ){
		if( !this->nodeExists( node, name ) ){
			ShowError( "Missing mandatory node \"%s\".\n", name.c_str() );
			missing = true;
		}
	}

	if( missing ){
		this->invalidWarning( node, "At least one mandatory node did not exist.\n" );
		return false;
	}

	return true;
}

bool YamlDatabase::verifyCompatibility( const ryml::Tree& tree ){
	if( !this->nodeExists( tree.rootref(), "Header" ) ){
		ShowError( "No database \"Header\" was found.\n" );
		return false;
	}

	const ryml::NodeRef headerNode = tree["Header"];

	if( !this->nodeExists( headerNode, "Type" ) ){
		ShowError( "No database \"Type\" was found.\n" );
		return false;
	}

	const ryml::NodeRef typeNode = headerNode["Type"];
	std::string tmpType;
	typeNode >> tmpType;

	if( this->type != tmpType ){
		ShowError( "Database type mismatch: %s != %s.\n", this->type.c_str(), tmpType.c_str() );
		return false;
	}

	uint16 tmpVersion;

	if( !this->asUInt16( headerNode, "Version", tmpVersion ) ){
		ShowError("Invalid header \"Version\" type for %s database.\n", this->type.c_str());
		return false;
	}

	if( tmpVersion != this->version ){
		if( tmpVersion > this->version ){
			ShowError( "Database version %hu is not supported. Maximum version is: %hu\n", tmpVersion, this->version );
			return false;
		}else if( tmpVersion >= this->minimumVersion ){
			ShowWarning( "Database version %hu is outdated and should be updated. Current version is: %hu\n", tmpVersion, this->version );
			ShowWarning( "Reduced compatibility with %s database file from '" CL_WHITE "%s" CL_RESET "'.\n", this->type.c_str(), this->currentFile.c_str() );
		}else{
			ShowError( "Database version %hu is not supported anymore. Minimum version is: %hu\n", tmpVersion, this->minimumVersion );
			return false;
		}
	}

	return true;
}

bool YamlDatabase::load(){
	bool ret = this->load( this->getDefaultLocation() );

	this->loadingFinished();

	return ret;
}

bool YamlDatabase::reload(){
	this->clear();

	return this->load();
}

bool YamlDatabase::load(const std::string& path) {
	ryml::Tree tree;
	char* buf = nullptr;
	ShowStatus("Loading '" CL_WHITE "%s" CL_RESET "'..." CL_CLL "\r", path.c_str());
	FILE* f = fopen(path.c_str(), "r");
	if (f == nullptr) {
		ShowError("Failed to open %s database file from '" CL_WHITE "%s" CL_RESET "'.\n", this->type, path.c_str());
		return false;
	}
	fseek(f, 0, SEEK_END);
	size_t size = ftell(f);
	buf = (char *)aMalloc(size+1);
	memset(buf, 0, size+1);
	rewind(f);
	fread(buf, sizeof(char), size, f);
	fclose(f);
	tree = ryml::parse(c4::to_substr(buf));

	// Required here already for header error reporting
	this->currentFile = path;

	if (!this->verifyCompatibility(tree)){
		ShowError("Failed to verify compatibility with %s database file from '" CL_WHITE "%s" CL_RESET "'.\n", this->type.c_str(), this->currentFile.c_str());
		aFree(buf);
		return false;
	}

	const ryml::NodeRef header = tree["Header"];

	if( this->nodeExists( header, "Clear" ) ){
		bool clear;

		if( this->asBool( header, "Clear", clear ) && clear ){
			this->clear();
		}
	}

	this->parse( tree );

	this->parseImports( tree );

	aFree(buf);
	return true;
}

void YamlDatabase::loadingFinished(){
	// Does nothing by default, just for hooking
}

void YamlDatabase::parse( const ryml::Tree& tree ){
	uint64 count = 0;

	if( this->nodeExists( tree.rootref(), "Body" ) ){
		const ryml::NodeRef bodyNode = tree["Body"];
		size_t childNodesCount = bodyNode.num_children();
		size_t childNodesProgressed = 0;
		const char* fileName = this->currentFile.c_str();

		for( ryml::NodeRef node : bodyNode.children() ){
			count += this->parseBodyNode( node );

			ShowStatus( "Loading [%" PRIdPTR "/%" PRIdPTR "] entries from '" CL_WHITE "%s" CL_RESET "'" CL_CLL "\r", ++childNodesProgressed, childNodesCount, fileName );
		}

		ShowStatus( "Done reading '" CL_WHITE "%" PRIu64 CL_RESET "' entries in '" CL_WHITE "%s" CL_RESET "'" CL_CLL "\n", count, fileName );
	}
}

void YamlDatabase::parseImports( const ryml::Tree& rootNode ){
	if( this->nodeExists( rootNode.rootref(), "Footer" ) ){
		const ryml::NodeRef footerNode = rootNode["Footer"];

		if( this->nodeExists( footerNode, "Imports") ){
			const ryml::NodeRef importsNode = footerNode["Imports"];
			for( const ryml::NodeRef node : importsNode.children() ){
				std::string importFile;

				if( !this->asString( node, "Path", importFile ) ){
					continue;
				}

				if( this->nodeExists( node, "Mode" ) ){
					std::string mode;

					if( !this->asString( node, "Mode", mode ) ){
						continue;
					}

#ifdef RENEWAL
					std::string compiledMode = "Renewal";
#else
					std::string compiledMode = "Prerenewal";
#endif

					if( compiledMode != mode ){
						// Skip this import
						continue;
					}
				}				

				this->load( importFile );
			}
		}
	}
}

template <typename R> bool YamlDatabase::asType( const ryml::NodeRef node, const std::string& name, R& out ){
	if( this->nodeExists( node, name ) ){
		const ryml::NodeRef dataNode = node[c4::to_csubstr(name)];

		dataNode >> out;

		return true;
	}else{
		this->invalidWarning( node, "Missing node \"%s\".\n", name.c_str() );
		return false;
	}
}

bool YamlDatabase::asBool(const ryml::NodeRef node, const std::string &name, bool &out) {
	const auto targetNode = node[c4::to_csubstr(name)];
	if (targetNode.val() == "true") {
		out = true;
		return true;
	}
	else if (targetNode.val() == "false") {
		out = false;
		return true;
	}
	return false;
}

bool YamlDatabase::asInt16( const ryml::NodeRef node, const std::string& name, int16& out ){
	return asType<int16>( node, name, out);
}

bool YamlDatabase::asUInt16(const ryml::NodeRef node, const std::string& name, uint16& out) {
	return asType<uint16>(node, name, out);
}

bool YamlDatabase::asInt32(const ryml::NodeRef node, const std::string &name, int32 &out) {
	return asType<int32>(node, name, out);
}

bool YamlDatabase::asUInt32(const ryml::NodeRef node, const std::string &name, uint32 &out) {
	return asType<uint32>(node, name, out);
}

bool YamlDatabase::asInt64(const ryml::NodeRef node, const std::string &name, int64 &out) {
	return asType<int64>(node, name, out);
}

bool YamlDatabase::asUInt64(const ryml::NodeRef node, const std::string &name, uint64 &out) {
	return asType<uint64>(node, name, out);
}

bool YamlDatabase::asFloat(const ryml::NodeRef node, const std::string &name, float &out) {
	return asType<float>(node, name, out);
}

bool YamlDatabase::asDouble(const ryml::NodeRef node, const std::string &name, double &out) {
	return asType<double>(node, name, out);
}

bool YamlDatabase::asString(const ryml::NodeRef node, const std::string &name, std::string &out) {
	return asType<std::string>(node, name, out);
}

bool YamlDatabase::asUInt16Rate( const ryml::NodeRef node, const std::string& name, uint16& out, uint16 maximum ){
	if( this->asUInt16( node, name, out ) ){
		if( out > maximum ){
			this->invalidWarning( node[c4::to_csubstr(name)], "Node \"%s\" with value %" PRIu16 " exceeds maximum of %" PRIu16 ".\n", name.c_str(), out, maximum );

			return false;
		}else if( out == 0 ){
			this->invalidWarning( node[c4::to_csubstr(name)], "Node \"%s\" needs to be at least 1.\n", name.c_str() );

			return false;
		}else{
			return true;
		}
	}else{
		return false;
	}
}

bool YamlDatabase::asUInt32Rate( const ryml::NodeRef node, const std::string& name, uint32& out, uint32 maximum ){
	if( this->asUInt32( node, name, out ) ){
		if( out > maximum ){
			this->invalidWarning( node[c4::to_csubstr(name)], "Node \"%s\" with value %" PRIu32 " exceeds maximum of %" PRIu32 ".\n", name.c_str(), out, maximum );

			return false;
		}else if( out == 0 ){
			this->invalidWarning( node[c4::to_csubstr(name)], "Node \"%s\" needs to be at least 1.\n", name.c_str() );

			return false;
		}else{
			return true;
		}
	}else{
		return false;
	}
}

void YamlDatabase::invalidWarning( const ryml::NodeRef node, const char* fmt, ... ){
	va_list ap;

	va_start(ap, fmt);

	// Remove any remaining garbage of a previous loading line
	ShowMessage( CL_CLL );
	// Print the actual error
	_vShowMessage( MSG_ERROR, fmt, ap );

	va_end(ap);

	ShowError( "Occurred in file '" CL_WHITE "%s" CL_RESET "' on line %d and column %d.\n", this->currentFile.c_str(), -1, -1 ); // TODO: Report line and col.

#ifdef DEBUG
	std::cout << node;
#endif
}

std::string YamlDatabase::getCurrentFile(){
	return this->currentFile;
}
