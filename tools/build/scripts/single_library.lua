SINGLE_LIBRARY_PLATFORM_PATTERNS = {
    "Android-*",
};

SINGLE_LIBRARY_FILTER =
    "platforms:" .. table.concat(SINGLE_LIBRARY_PLATFORM_PATTERNS, " or ");
NOT_SINGLE_LIBRARY_FILTER = table.translate(
    SINGLE_LIBRARY_PLATFORM_PATTERNS,
    function(pattern)
      return "platforms:not " .. pattern;
    end);

function single_library_windowed_app_kind()
  filter(SINGLE_LIBRARY_FILTER);
    kind("StaticLib");
    wholelib("On");
  filter(NOT_SINGLE_LIBRARY_FILTER);
    kind("WindowedApp");
  filter({});
end

-- Adds the platform-specific windowed app entry point (and what it needs to
-- link) to the current project. Requires the including script to have set
-- project_root.
function windowed_app_main_files()
  filter("platforms:not Mac")
    files({
      project_root.."/src/xenia/ui/windowed_app_main_"..platform_suffix..".cc",
    })
  filter("platforms:Mac")
    files({
      project_root.."/src/xenia/ui/windowed_app_main_mac.mm",
    })
    links({
      "AppKit.framework",
      "QuartzCore.framework",
      "UniformTypeIdentifiers.framework",
    })
  filter({})
end
