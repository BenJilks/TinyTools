#pragma once
#include <string>
#include <map>
#include <vector>
#include <memory>
#include "modules/module.hpp"

class Shell
{
public:
	Shell();

	static Shell &the();

	void prompt();
	void run();

	void set(std::string env, const std::string &value);
	std::string get(std::string env);

	template<typename ModuleType>
	void add_module()
	{
		modules.push_back(std::make_unique<ModuleType>());
	}

private:
	void exec_line(const std::string &line);

	std::map<std::string, std::string> env_buffer;
	std::vector<std::string> command_history;
	std::vector<std::unique_ptr<Module>> modules;

};

