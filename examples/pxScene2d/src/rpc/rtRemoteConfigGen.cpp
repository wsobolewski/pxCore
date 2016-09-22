#include <exception>
#include <memory>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include <stdint.h>

#include <assert.h>
#include <ctype.h>
#include <getopt.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>

#include <rapidjson/document.h>
#include <rapidjson/filereadstream.h>
#include <rapidjson/error/error.h>
#include <rapidjson/error/en.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

struct ConfigItem
{
  std::string Name;
  std::string DefaultValue;
  std::string Platform;
  std::string Type;
  std::string Json;

  bool operator < (ConfigItem const& rhs) const
  {
    return Name < rhs.Name;
  }
};

static std::string getMemberName(ConfigItem const& item)
{
  std::stringstream buff;

  char const* p = item.Name.c_str();
  if (strncmp(p, "rt.rpc.", 7) == 0)
    p += 7;

  while (p && *p)
  {
    if (*p == '.')
      buff << '_';
    else
      buff << *p;
    ++p;
  }

  return buff.str();
}

static std::string getBuilderGetter(ConfigItem const& item)
{
  std::map< std::string, std::string > m = 
  {
    { "string", "getString" },
    { "uint16", "getUInt16" },
    { "uint32", "getUInt32" },
    { "bool",   "getBool" },
    { "double", "getDouble" },
    { "float",  "getFloat" },
    { "int32",  "getInt32" },
    { "int64",  "getInt64" },
    { "uint64", "getUInt64" }
  };

  auto itr = m.find(item.Type);
  return itr != m.end() 
    ? itr->second
    : "UNKNOWN_TYPE";
}

static std::string getType(ConfigItem const& item)
{
  std::map< std::string, std::string > m = 
  {
    { "int32", "int32_t" },
    { "uint32", "uint32_t" },
    { "string", "std::string" },
    { "int16", "int16_t" },
    { "uint16", "uint16_t" },
    { "int64", "int64_t" },
    { "uint64", "uint64_t" },
    { "float", "float" },
    { "double", "double" },
    { "bool", "bool" }
  };

  auto itr = m.find(item.Type);
  return itr != m.end()
    ? itr->second
    : "UNKNOWN_TYPE";
}

static void printGetter(FILE* out, ConfigItem const& item)
{
  static const char* kDelimiter = ".";

  char* s = strdup(item.Name.c_str());
  char* p = strtok(s, kDelimiter);
  fprintf(out, "  inline %s %s() const\n", getType(item).c_str(),
    getMemberName(item).c_str());

  std::string name;
  while (p)
  {
    p = strtok(NULL, kDelimiter);
    p = strtok(NULL, kDelimiter);
  }
  fprintf(out, "    { return m_%s; }\n", getMemberName(item).c_str());
  free(s);
}

static void processConfigParamList(FILE* f, rapidjson::Value const& configParamsList,
  std::function<void (FILE* , ConfigItem const& )> func)
{
  for (rapidjson::SizeType i = 0; i < configParamsList.Size(); ++i)
  {
    rapidjson::Value const& configParam = configParamsList[i];

    ConfigItem item;
    item.Name = configParam["name"].GetString();
    item.DefaultValue = configParam["default_value"].GetString();
    item.Type = configParam["type"].GetString();

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    configParam.Accept(writer);
    item.Json = buffer.GetString();

    auto itr = configParam.FindMember("platform");
    if (itr != configParam.MemberEnd())
      item.Platform = itr->value.GetString();

    func(f, item);
  }
}

static std::set<ConfigItem> buildConfigItems(rapidjson::Value const& configParamsList)
{
  std::set<ConfigItem> configItems;
  processConfigParamList(nullptr, configParamsList, [&configItems](FILE*, ConfigItem const& item)
  {
    if (item.Platform.size() > 0)
    {
    #ifdef __APPLE__
      if (strcasecmp(item.Platform.c_str(), "mac") == 0)
        configItems.insert(item);
    #endif

    #ifdef __linux__
      if (strcasecmp(item.Platform.c_str(), "linux") == 0)
        configItems.insert(item);
    #endif
    }
    else
    {
      configItems.insert(item);
    }
  });
  return std::move(configItems);
}


static int safeClose(FILE* f)
{
  if (f) fclose(f);
  return 0;
}

static void printHeader(FILE* f, char const* fname)
{
  char buff[256];

  time_t now = time(nullptr);
  struct tm* tmp = localtime(&now);
  strftime(buff, sizeof(buff), "%a, %d %b %y %T %z", tmp);
  fprintf(f, "// %s\n", fname);
  fprintf(f, "// DO NOT EDIT -- AUTOGENERATED SOURCE FILE: %s\n", buff);
}

static void dumpOffset(FILE* f, int offset)
{
  if (offset > 16)
    offset -= 16;
  else
    offset = 0;

  fseek(f, offset, SEEK_SET);

  char buff[64];
  memset(buff, 0, sizeof(buff));
  int unused = fread(buff, sizeof(buff), 1, f);
  (void)(unused);
  buff[63] = '\0';
  fprintf(stderr, "----- failed chunk ----\n");
  fprintf(stderr, "'%s'\n", buff);
  fprintf(stderr, "-----------------------\n");
}

static bool doGenConfig(rapidjson::Document const& doc, std::string const& outfile)
{

  rapidjson::Value::ConstMemberIterator itr = doc.FindMember("config_params");
  if (itr == doc.MemberEnd())
  {
    fprintf(stderr, "failed to find config_params array\n");
    return false;
  }

  std::set<ConfigItem> configItems = buildConfigItems(itr->value);

  std::unique_ptr<FILE, int (*)(FILE *)> out(fopen(outfile.c_str(), "w"), safeClose);
  for (auto i: configItems)
    fprintf(out.get(), "%s=%s\n", i.Name.c_str(), i.DefaultValue.c_str());

  return true;
}

static bool doGenerateHeader(rapidjson::Document const& doc, std::string const& outfile)
{
  std::unique_ptr<FILE, int (*)(FILE *)> out(fopen(outfile.c_str(), "w"), safeClose);
  printHeader(out.get(), outfile.c_str());
  fprintf(out.get(), "#ifndef __RT_REMOTE_CONFIG_H__\n");
  fprintf(out.get(), "#define __RT_REMOTE_CONFIG_H__\n");
  fprintf(out.get(), "#include <string>\n");
  fprintf(out.get(), "#include <stdint.h>\n\n");
  fprintf(out.get(), "class rtRemoteConfig\n");
  fprintf(out.get(), "{\n");

  rapidjson::Value::ConstMemberIterator itr = doc.FindMember("config_params");
  if (itr == doc.MemberEnd())
  {
    fprintf(stderr, "failed to find config_params array\n");
    return false;
  }

  std::set<ConfigItem> configItems = buildConfigItems(itr->value);

  fprintf(out.get(), "public:\n");
  for (auto i : configItems)
  {
    fprintf(out.get(), "  // %s\n", i.Json.c_str());
    printGetter(out.get(), i);
    fprintf(out.get(), "  inline void set_%s(%s arg)\n", getMemberName(i).c_str(),
        getType(i).c_str());
    fprintf(out.get(), "    { m_%s = arg; }\n", getMemberName(i).c_str());
    fprintf(out.get(), "\n\n");
  }

  fprintf(out.get(), "\n");
  fprintf(out.get(), "private:\n");
  for (auto i : configItems)
    fprintf(out.get(), "  %-15s m_%s;\n", getType(i).c_str(), getMemberName(i).c_str());

  fprintf(out.get(), "};\n\n");
  fprintf(out.get(), "#endif\n");
  fprintf(out.get(), "// END-OF-FILE\n");

  return true;
}

static bool doGenerateSource(rapidjson::Document const& doc, std::string const& outfile)
{
  std::unique_ptr<FILE, int (*)(FILE *)> out(fopen(outfile.c_str(), "w"), safeClose);
  printHeader(out.get(), outfile.c_str());

  fprintf(out.get(), "#include \"rtRemoteConfig.h\"\n");
  // fprintf(out.get(), "#include \"rtRemoteTypes.h\"\n");
  fprintf(out.get(), "#include \"rtRemoteConfigBuilder.h\"\n\n");
  fprintf(out.get(), "rtRemoteConfig*\n");
  fprintf(out.get(), "rtRemoteConfigBuilder::build() const\n");
  fprintf(out.get(), "{\n");
  fprintf(out.get(), "  rtRemoteConfig* conf(new rtRemoteConfig());\n");

  rapidjson::Value::ConstMemberIterator itr = doc.FindMember("config_params");
  if (itr == doc.MemberEnd())
  {
    fprintf(stderr, "failed to find config_params array\n");
    return false;
  }

  std::set<ConfigItem> configItems = buildConfigItems(itr->value);

  for (auto i : configItems)
  {
    fprintf(out.get(), "\n  // %s\n", i.Json.c_str());
    fprintf(out.get(), "  // WARNING: default may have been overridden by configuration file\n");
    fprintf(out.get(), "  {\n");
    fprintf(out.get(), "    %s const val = this->%s(\"%s\");\n", getType(i).c_str(),
        getBuilderGetter(i).c_str(), i.Name.c_str());
    fprintf(out.get(), "    conf->set_%s(val);\n", getMemberName(i).c_str());
    fprintf(out.get(), "  }\n");
  }

  fprintf(out.get(), "\n  return conf;\n");
  fprintf(out.get(), "}\n");

  fprintf(out.get(), "\n\n");
  fprintf(out.get(), "rtRemoteConfigBuilder::rtRemoteConfigBuilder()\n");
  fprintf(out.get(), "{\n");
  for (auto i : configItems)
  {
    fprintf(out.get(), "  // %s\n", i.Json.c_str());
    fprintf(out.get(), "  m_map.insert(std::map<std::string, std::string>::value_type(\"%s\", \"%s\"));\n",
      i.Name.c_str(), i.DefaultValue.c_str());
    fprintf(out.get(), "\n");
  }
  fprintf(out.get(), "}\n");
  fprintf(out.get(), "// END-OF-FILE\n");

  return true;
}


int main(int argc, char* argv[])
{
  bool        gen_header = false;
  bool        gen_source = false;
  bool        gen_config = false;

  std::string infile;
  std::string outfile;

  int opt;
  while ((opt = getopt(argc, argv, "i:hcso:")) != -1)
  {
    switch (opt)
    {
      case 'i':
        infile = optarg;
        break;

      case 'c':
        gen_config = true;
        break;

      case 'o':
        outfile = optarg;
        break;

      case 'h':
        gen_header = true;
        break;

      case 's':
        gen_source = true;
        break;

      default:
        assert(false);
        break;
    }
  }

  std::unique_ptr<FILE, int (*)(FILE *)> f(fopen(infile.c_str(), "r"), safeClose);
  if (!f)
  {
    return 1;
  }

  std::vector<char> buff(1024 * 64);

  rapidjson::Document doc;
  rapidjson::FileReadStream stream(f.get(), &buff[0], buff.size());
  rapidjson::ParseResult result = doc.ParseStream(stream);

  if (result)
  {
    if (gen_header)
    {
      if (!doGenerateHeader(doc, outfile))
        return 1;
    }

    if (gen_source)
    {
      if (!doGenerateSource(doc, outfile))
        return 1;
    }

    if (gen_config)
    {
      if (!doGenConfig(doc, outfile))
        return 1;
    }
  }
  else
  {
    rapidjson::ParseErrorCode e = doc.GetParseError();
    fprintf(stderr, "JSON parse error: %s (%lu)\n",
      rapidjson::GetParseError_En(e), result.Offset());

    dumpOffset(f.get(), result.Offset());
    return 1;
  }

  return 0;
}