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
  -- Premake suffixes macOS WindowedApp targets with ".app" even though gmake
  -- produces a plain executable, not a bundle - and macOS stalls the exec of
  -- plain files named *.app in Gatekeeper policy evaluation.
  filter({"platforms:Mac"});
    targetextension("");
  filter({});
end
