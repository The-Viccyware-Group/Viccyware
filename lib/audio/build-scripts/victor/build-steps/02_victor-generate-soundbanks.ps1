$slack_channel = $env:SLACK_CHANNEL
$wwise_cle_exe = $env:WWISE_CLI_EXE
$env:path = "$env:Path;c:\tools\python2"
$branch = $env:SVN_BRANCH
$branch = $branch -replace "/","\"
$_REPO_LOCATION = [string](Get-Location)
$_PROJ_DIR = "$_REPO_LOCATION\$branch\VictorAudio"
$_GSB_DIR = "$_PROJ_DIR\GeneratedSoundBanks"

echo "INFO - env.SLACK_CHANNEL`: $slack_channel"
echo "INFO - env.WWISE_CLI_EXE`: $wwise_cle_exe"
echo "INFO - env._REPO_LOCATION: $_REPO_LOCATION"
echo "INFO - env._PYTHON_FIX_M4A_SCRIPT_PATH: $_PYTHON_FIX_M4A_SCRIPT_PATH"
echo "INFO - env._PYTHON_REMOVE_UNUSED_SCRIPT_PATH: $_PYTHON_REMOVE_UNUSED_SCRIPT_PATH"
echo "INFO - env._PROJ_DIR : $_PROJ_DIR"
echo "INFO - env._GSB_DIR : $_GSB_DIR"


# send a failure message to slack channel
function SendFailureSlackMessage([String] $error_message) {
  $payload = @{
      channel = $slack_channel;
      username = "buildbot";
      attachments = @(
          @{
          text = $error_message;
          fallback = $error_message;
          color = "danger";
          };
      );
  }
  Invoke-Restmethod -Method POST -Body ($payload | ConvertTo-Json -Depth 4) -Uri $env:SLACK_TOKEN_URL
}

# Check exit code and maybe send a failure message to slack
function CheckExitCode([Int] $exit_code, [String] $error_message) {
    if ($exit_code -ne 0) {
      echo "INFO - Exiting script. $error_message Build failed. exitCode $exit_code"
      SendFailureSlackMessage "$error_message Build failed. exitCode $exit_code"
      exit $exit_code
    }
}

function CheckWWiseExitCode([Int] $exit_code) {
    if (($exit_code -ne 0) -and ($exit_code -ne 2)) {
      echo "INFO - Exiting script. WWise build failed. exitCode $exit_code"
      SendFailureSlackMessage "WWise build failed. exitCode $exit_code"
      exit $exit_code
    }
}


# Delete GeneratedSoundBanks Dir
if (Test-Path -Path $_GSB_DIR) {
    Remove-Item $_GSB_DIR\* -recurse
}

& $wwise_cle_exe "$branch\VictorAudio\VictorAudio.wproj" -GenerateSoundBanks -Verbose
CheckWWiseExitCode $lastExitCode

exit 0
