#include <RED4ext/RED4ext.hpp>
#include <RED4ext/Api/v1/Runtime.hpp>
#include <RED4ext/Api/v1/GameState.hpp>
#include <RED4ext/Api/v1/GameStates.hpp>

#include <pugixml.hpp>

#include <Windows.h>

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace InputLoaderReborn
{
    namespace fs = std::filesystem;

    fs::path g_pluginDirectory;
    fs::path g_gameDirectory;

    pugi::xml_document g_inputContexts;
    pugi::xml_document g_inputMappings;

    bool g_originalsLoaded = false;

    // Files registered by other RED4ext plugins through Add().
    std::vector<fs::path> g_dynamicFiles;

    // ---------------------------------------------------------
    // Logging
    // ---------------------------------------------------------

    void ClearLog()
    {
        if (g_pluginDirectory.empty())
            return;

        // Truncate the log once when the plugin loads so each game
        // session starts with a fresh loader.log.
        std::ofstream log(
            g_pluginDirectory / "loader.log",
            std::ios::out | std::ios::trunc
        );
    }

    void WriteLog(const std::string& message)
    {
        if (g_pluginDirectory.empty())
            return;

        std::ofstream log(
            g_pluginDirectory / "loader.log",
            std::ios::app
        );

        if (log.is_open())
            log << message << '\n';
    }

    // ---------------------------------------------------------
    // Paths
    // ---------------------------------------------------------

    bool InitializePaths(RED4ext::v1::PluginHandle aHandle)
    {
        wchar_t modulePath[MAX_PATH]{};

        if (GetModuleFileNameW(
            aHandle,
            modulePath,
            MAX_PATH) == 0)
        {
            return false;
        }

        g_pluginDirectory =
            fs::path(modulePath).parent_path();

        // Cyberpunk 2077/
        // └── red4ext/
        //     └── plugins/
        //         └── InputLoaderReborn/
        //             └── InputLoaderReborn.dll

        g_gameDirectory =
            g_pluginDirectory
            .parent_path()
            .parent_path()
            .parent_path();

        return true;
    }

    // ---------------------------------------------------------
    // XML loading
    // ---------------------------------------------------------

    bool LoadXmlDocument(
        const fs::path& path,
        pugi::xml_document& document)
    {
        const std::string pathString = path.string();

        pugi::xml_parse_result result =
            document.load_file(pathString.c_str());

        if (!result)
        {
            WriteLog(
                "[InputLoaderReborn] ERROR parsing: " +
                pathString
            );

            WriteLog(
                "[InputLoaderReborn] pugixml: " +
                std::string(result.description())
            );

            WriteLog(
                "[InputLoaderReborn] Error offset: " +
                std::to_string(result.offset)
            );

            return false;
        }

        WriteLog(
            "[InputLoaderReborn] Loaded: " +
            pathString
        );

        return true;
    }

    // ---------------------------------------------------------
    // Load vanilla Cyberpunk input configuration
    // ---------------------------------------------------------

    bool LoadOriginalInputFiles()
    {
        WriteLog("");
        WriteLog(
            "[InputLoaderReborn] Loading original game input files..."
        );

        const fs::path contextsPath =
            g_gameDirectory /
            "r6/config/inputContexts.xml";

        const fs::path mappingsPath =
            g_gameDirectory /
            "r6/config/inputUserMappings.xml";

        g_inputContexts.reset();
        g_inputMappings.reset();

        if (!LoadXmlDocument(
            contextsPath,
            g_inputContexts))
        {
            WriteLog(
                "[InputLoaderReborn] FATAL: Could not load inputContexts.xml."
            );

            return false;
        }

        if (!LoadXmlDocument(
            mappingsPath,
            g_inputMappings))
        {
            WriteLog(
                "[InputLoaderReborn] Main inputUserMappings.xml failed."
            );

            // Try a backup beside our DLL.
            const fs::path localBackup =
                g_pluginDirectory /
                "inputUserMappings.xml";

            if (fs::exists(localBackup))
            {
                WriteLog(
                    "[InputLoaderReborn] Trying plugin backup..."
                );

                g_inputMappings.reset();

                if (LoadXmlDocument(
                    localBackup,
                    g_inputMappings))
                {
                    WriteLog(
                        "[InputLoaderReborn] Backup loaded successfully."
                    );

                    g_originalsLoaded = true;
                    return true;
                }
            }

            // Also check the legacy Input Loader location.
            const fs::path legacyBackup =
                g_gameDirectory /
                "red4ext/plugins/input_loader/inputUserMappings.xml";

            if (fs::exists(legacyBackup))
            {
                WriteLog(
                    "[InputLoaderReborn] Trying legacy Input Loader backup..."
                );

                g_inputMappings.reset();

                if (LoadXmlDocument(
                    legacyBackup,
                    g_inputMappings))
                {
                    WriteLog(
                        "[InputLoaderReborn] Legacy backup loaded successfully."
                    );

                    g_originalsLoaded = true;
                    return true;
                }
            }

            WriteLog(
                "[InputLoaderReborn] FATAL: No valid inputUserMappings.xml available."
            );

            return false;
        }

        g_originalsLoaded = true;

        WriteLog(
            "[InputLoaderReborn] Original input configuration loaded successfully."
        );

        return true;
    }

    // ---------------------------------------------------------
    // Node classification
    // ---------------------------------------------------------

    bool IsContextNode(const std::string& name)
    {
        static const std::vector<std::string> contextNodes =
        {
            "blend",
            "context",
            "hold",
            "multitap",
            "repeat",
            "toggle",
            "acceptedEvents"
        };

        return std::find(
            contextNodes.begin(),
            contextNodes.end(),
            name
        ) != contextNodes.end();
    }

    bool IsMappingNode(const std::string& name)
    {
        static const std::vector<std::string> mappingNodes =
        {
            "mapping",
            "buttonGroup",
            "pairedAxes",
            "preset"
        };

        return std::find(
            mappingNodes.begin(),
            mappingNodes.end(),
            name
        ) != mappingNodes.end();
    }

    // ---------------------------------------------------------
    // Merge one binding into one of the master documents
    // ---------------------------------------------------------

    bool MergeBinding(pugi::xml_node modNode)
    {
        const std::string nodeType =
            modNode.name();

        pugi::xml_document* destinationDocument = nullptr;

        if (IsContextNode(nodeType))
        {
            destinationDocument =
                &g_inputContexts;
        }
        else if (IsMappingNode(nodeType))
        {
            destinationDocument =
                &g_inputMappings;
        }
        else
        {
            WriteLog(
                "[InputLoaderReborn] WARNING: Unsupported binding type <" +
                nodeType +
                "> - skipped."
            );

            return false;
        }

        pugi::xml_node destinationBindings =
            destinationDocument->child("bindings");

        if (!destinationBindings)
        {
            WriteLog(
                "[InputLoaderReborn] ERROR: Destination XML has no <bindings> root."
            );

            return false;
        }

        const char* nodeName =
            modNode.attribute("name").as_string();

        pugi::xml_node existingNode;

        if (nodeName[0] != '\0')
        {
            existingNode =
                destinationBindings.find_child_by_attribute(
                    nodeType.c_str(),
                    "name",
                    nodeName
                );
        }

        // -----------------------------------------------------
        // Existing definition
        // -----------------------------------------------------

        if (existingNode)
        {
            const bool append =
                modNode.attribute("append").as_bool(false);

            if (append)
            {
                WriteLog(
                    "[InputLoaderReborn] APPEND: <" +
                    nodeType +
                    "> name=\"" +
                    nodeName +
                    "\""
                );

                for (pugi::xml_node child :
                modNode.children())
                {
                    existingNode.append_copy(child);
                }
            }
            else
            {
                WriteLog(
                    "[InputLoaderReborn] REPLACE: <" +
                    nodeType +
                    "> name=\"" +
                    nodeName +
                    "\""
                );

                destinationBindings.remove_child(existingNode);

                destinationBindings.append_copy(modNode);
            }
        }

        // -----------------------------------------------------
        // New definition
        // -----------------------------------------------------

        else
        {
            std::string description =
                "[InputLoaderReborn] ADD: <" +
                nodeType +
                ">";

            if (nodeName[0] != '\0')
            {
                description +=
                    " name=\"" +
                    std::string(nodeName) +
                    "\"";
            }

            WriteLog(description);

            destinationBindings.append_copy(modNode);
        }

        return true;
    }

    // ---------------------------------------------------------
    // Merge one mod XML file
    // ---------------------------------------------------------

    bool MergeModFile(const fs::path& path)
    {
        WriteLog("");
        WriteLog(
            "[InputLoaderReborn] Processing: " +
            path.filename().string()
        );

        pugi::xml_document modDocument;

        if (!LoadXmlDocument(
            path,
            modDocument))
        {
            WriteLog(
                "[InputLoaderReborn] FAILED: " +
                path.filename().string()
            );

            return false;
        }

        pugi::xml_node bindings =
            modDocument.child("bindings");

        if (!bindings)
        {
            WriteLog(
                "[InputLoaderReborn] WARNING: No <bindings> root found in " +
                path.filename().string()
            );

            return false;
        }

        int processed = 0;

        for (pugi::xml_node node :
        bindings.children())
        {
            // Ignore comments, text nodes, etc.
            if (node.type() != pugi::node_element)
                continue;

            if (MergeBinding(node))
                ++processed;
        }

        WriteLog(
            "[InputLoaderReborn] Finished " +
            path.filename().string() +
            " - " +
            std::to_string(processed) +
            " binding block(s) processed."
        );

        return true;
    }

    // ---------------------------------------------------------
    // r6/input scanner
    // ---------------------------------------------------------

    void MergeInputDirectory()
    {
        const fs::path inputDirectory =
            g_gameDirectory /
            "r6/input";

        try
        {
            if (!fs::exists(inputDirectory))
            {
                fs::create_directories(
                    inputDirectory
                );
            }

            int loadedFiles = 0;

            for (const auto& entry :
                fs::recursive_directory_iterator(
                    inputDirectory))
            {
                if (!entry.is_regular_file())
                    continue;

                std::wstring extension =
                    entry.path().extension().wstring();

                std::transform(
                    extension.begin(),
                    extension.end(),
                    extension.begin(),
                    [](wchar_t character)
                    {
                        return static_cast<wchar_t>(
                            std::towlower(character)
                            );
                    }
                );

                if (extension != L".xml")
                    continue;

                if (MergeModFile(entry.path()))
                    ++loadedFiles;
            }

            WriteLog("");
            WriteLog(
                "[InputLoaderReborn] r6/input XML files loaded: " +
                std::to_string(loadedFiles)
            );
        }
        catch (const std::exception& error)
        {
            WriteLog(
                "[InputLoaderReborn] ERROR scanning r6/input: " +
                std::string(error.what())
            );
        }
    }

    // ---------------------------------------------------------
    // Dynamically registered XML files
    // ---------------------------------------------------------

    void MergeDynamicFiles()
    {
        if (g_dynamicFiles.empty())
        {
            WriteLog(
                "[InputLoaderReborn] No dynamically registered input files."
            );

            return;
        }

        WriteLog(
            "[InputLoaderReborn] Processing dynamically registered input files..."
        );

        for (const fs::path& path :
            g_dynamicFiles)
        {
            MergeModFile(path);
        }
    }

    // ---------------------------------------------------------
    // Save merged configuration
    // ---------------------------------------------------------

    bool SaveMergedFiles()
    {
        const fs::path cacheDirectory =
            g_gameDirectory /
            "r6/cache";

        try
        {
            fs::create_directories(
                cacheDirectory
            );
        }
        catch (const std::exception& error)
        {
            WriteLog(
                "[InputLoaderReborn] ERROR creating r6/cache: " +
                std::string(error.what())
            );

            return false;
        }

        const fs::path contextsOutput =
            cacheDirectory /
            "inputContexts.xml";

        const fs::path mappingsOutput =
            cacheDirectory /
            "inputUserMappings.xml";

        const bool contextsSaved =
            g_inputContexts.save_file(
                contextsOutput.string().c_str(),
                "  "
            );

        const bool mappingsSaved =
            g_inputMappings.save_file(
                mappingsOutput.string().c_str(),
                "  "
            );

        if (contextsSaved)
        {
            WriteLog(
                "[InputLoaderReborn] SAVED: r6/cache/inputContexts.xml"
            );
        }
        else
        {
            WriteLog(
                "[InputLoaderReborn] ERROR saving inputContexts.xml"
            );
        }

        if (mappingsSaved)
        {
            WriteLog(
                "[InputLoaderReborn] SAVED: r6/cache/inputUserMappings.xml"
            );
        }
        else
        {
            WriteLog(
                "[InputLoaderReborn] ERROR saving inputUserMappings.xml"
            );
        }

        return contextsSaved && mappingsSaved;
    }

    // ---------------------------------------------------------
    // BaseInitialization callback
    // ---------------------------------------------------------

    bool OnBaseInitializationFinished(
        RED4ext::CGameApplication*)
    {
        WriteLog("");
        WriteLog("========================================");
        WriteLog(
            "[InputLoaderReborn] BaseInitialization finished."
        );
        WriteLog(
            "[InputLoaderReborn] Starting input merge."
        );
        WriteLog("========================================");

        if (!g_originalsLoaded)
        {
            WriteLog(
                "[InputLoaderReborn] FATAL: Original XML documents were not loaded."
            );

            return true;
        }

        MergeInputDirectory();
        MergeDynamicFiles();

        if (SaveMergedFiles())
        {
            WriteLog("");
            WriteLog("========================================");
            WriteLog(
                "[InputLoaderReborn] INPUT MERGE COMPLETE"
            );
            WriteLog("========================================");
        }
        else
        {
            WriteLog(
                "[InputLoaderReborn] ERROR: Input merge did not complete successfully."
            );
        }

        return true;
    }
}

// =============================================================
// Compatibility export
//
// Allows plugins designed around Input Loader's Add() interface
// to register XML files.
// =============================================================

RED4EXT_C_EXPORT void Add(
    RED4ext::v1::PluginHandle aHandle,
    const wchar_t* aPath)
{
    if (aPath == nullptr)
        return;

    std::filesystem::path path(aPath);

    // Relative paths are relative to the calling plugin DLL.
    if (path.is_relative())
    {
        wchar_t callerPath[MAX_PATH]{};

        if (GetModuleFileNameW(
            aHandle,
            callerPath,
            MAX_PATH) > 0)
        {
            path =
                std::filesystem::path(callerPath)
                .parent_path() /
                path;
        }
    }

    InputLoaderReborn::g_dynamicFiles.push_back(
        path
    );

    InputLoaderReborn::WriteLog(
        "[InputLoaderReborn] Dynamically registered XML: " +
        path.string()
    );
}

// =============================================================
// RED4ext
// =============================================================

RED4EXT_C_EXPORT bool RED4EXT_CALL Main(
    RED4ext::v1::PluginHandle aHandle,
    RED4ext::v1::EMainReason aReason,
    const RED4ext::v1::Sdk* aSdk)
{
    switch (aReason)
    {
    case RED4ext::v1::EMainReason::Load:
    {
        if (!InputLoaderReborn::InitializePaths(
            aHandle))
        {
            return false;
        }

        InputLoaderReborn::ClearLog();

        InputLoaderReborn::WriteLog("");
        InputLoaderReborn::WriteLog("========================================");
        InputLoaderReborn::WriteLog(
            "[InputLoaderReborn] Plugin loaded."
        );
        InputLoaderReborn::WriteLog(
            "[InputLoaderReborn] RED4ext API v1."
        );
        InputLoaderReborn::WriteLog("========================================");

        if (aSdk == nullptr ||
            aSdk->gameStates == nullptr)
        {
            InputLoaderReborn::WriteLog(
                "[InputLoaderReborn] FATAL: GameStates API unavailable."
            );

            return false;
        }

        // Load clean vanilla XML documents BEFORE we begin merging.
        if (!InputLoaderReborn::LoadOriginalInputFiles())
        {
            InputLoaderReborn::WriteLog(
                "[InputLoaderReborn] FATAL: Could not initialize base input configuration."
            );

            return false;
        }

        RED4ext::v1::GameState initState{};

        initState.OnEnter = nullptr;
        initState.OnUpdate = nullptr;
        initState.OnExit =
            &InputLoaderReborn::OnBaseInitializationFinished;

        const bool registered =
            aSdk->gameStates->Add(
                aHandle,
                RED4ext::EGameStateType::BaseInitialization,
                &initState
            );

        if (!registered)
        {
            InputLoaderReborn::WriteLog(
                "[InputLoaderReborn] FATAL: Failed to register BaseInitialization callback."
            );

            return false;
        }

        InputLoaderReborn::WriteLog(
            "[InputLoaderReborn] BaseInitialization callback registered."
        );

        break;
    }

    case RED4ext::v1::EMainReason::Unload:
    {
        InputLoaderReborn::WriteLog(
            "[InputLoaderReborn] Plugin unloaded."
        );

        break;
    }
    }

    return true;
}

RED4EXT_C_EXPORT void RED4EXT_CALL Query(
    RED4ext::v1::PluginInfo* aInfo)
{
    aInfo->sdk =
        RED4EXT_V1_SEMVER(1, 30, 0);

    aInfo->name =
        L"InputLoaderReborn";

    aInfo->author =
        L"Keen";

    aInfo->version =
        RED4EXT_V1_SEMVER(1, 0, 0);

    aInfo->runtime =
        RED4EXT_V1_RUNTIME_VERSION_INDEPENDENT;
}

RED4EXT_C_EXPORT uint32_t RED4EXT_CALL Supports()
{
    return 1;
}