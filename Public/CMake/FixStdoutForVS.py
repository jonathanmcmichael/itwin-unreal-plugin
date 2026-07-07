#--------------------------------------------------------------------------------------
#
#     $Source: FixStdoutForVS.py $
#
#  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
#
#--------------------------------------------------------------------------------------

import sys
import re
import os

# This script is designed to to be executed in a command pipeline, taking its input from an Unreal tool,
# eg. "RunUAT.bat xxx | python FixStdoutForVS.py".
# It should also work with a pnpm command.
# It filters the "false positive" errors that would otherwise be considered as real errors by Visual Studio.
# One issue with this method is that the exit code of the Unreal tool is ignored:
# the exit code of the pipeline is the exit code of the last process (ie. this script).
# So we need to somehow get the exit code of the Unreal tool and return it.
# Fortunately, the Unreal tools we use (often) print the exit code in their stdout, so we can retrieve it from there.

errorRegex = re.compile("error", re.IGNORECASE)
inputCommandInfos = {
	"uat": {
		"exitCodeRegex": r"ExitCode=(?P<exitCode>\d+)",
		"errorRegex": r"BUILD FAILED", # RunUAT.bat may fail without printing any exit code
	},
	"pnpm": {"exitCodeRegex": r"Command failed with exit code (?P<exitCode>\d+)"},
	"openCppCoverage": {
		"exitCodeRegex": r"Your program stop with error code: (?P<exitCode>-?\d+)",
		"ignoredExitCodes": [
			-2147483645 # 0x80000003: "false error" due to breakpoint instruction (eg. __debugbreak)
		],
		# When a breakpoint instruction occurs (which we want to ignore), then OpenCppCoverage returns
		# the (ignored) exit code above, even if there is a real test failure.
		# So we detect the regex below, which is printed by Catch2 when at least one test fails
		# (it does not print it when all tests pass).
		"errorRegex": r"assertions:\s+\d+\s+|\s+\d+\s+passed\s+|\s+\d+\s+failed",
	},
}
for info in inputCommandInfos.values():
	info["exitCodeRegexCompiled"] = re.compile(info["exitCodeRegex"], re.IGNORECASE)
	if "errorRegex" in info:
		info["errorRegex"] = re.compile(info["errorRegex"], re.IGNORECASE)
exitCode = None
isExitCodeIgnored = False
errorLine = None
for line in sys.stdin:
	# Replace all errors messages (real errors or false positives).
	# Replacing real errors is not an issue, as we rely on the exit code to inform VS
	# about whether the Unreal tool succeeded or failed.
	sys.stdout.write(errorRegex.sub("<error>", line))
	sys.stdout.flush()
	# Try to detect the exit code.
	for info in inputCommandInfos.values():
		exitCodeMatch = info["exitCodeRegexCompiled"].search(line)
		if exitCodeMatch != None:
			exitCode = int(exitCodeMatch.group("exitCode"))
			if exitCode in info.get("ignoredExitCodes", []):
				isExitCodeIgnored = True
		if errorLine == None and "errorRegex" in info and info["errorRegex"].search(line) != None:
			errorLine = line
print(f"{os.path.basename(__file__)}: last exit code found in stdin: {exitCode}")
if isExitCodeIgnored:
	print(f"{os.path.basename(__file__)}: exit code is ignored")
	exitCode = 0
if errorLine != None:
	print(f"{os.path.basename(__file__)}: first error string found in stdin:\n{errorLine}")
else:
	print(f"{os.path.basename(__file__)}: no error line found in stdin")
exitCode = exitCode or (1 if errorLine is not None else 0)
print(f"{os.path.basename(__file__)}: final exit code that will be returned: {exitCode}")
sys.exit(exitCode)
