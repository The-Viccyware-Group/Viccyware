$slack_channel = $env:SLACK_CHANNEL
$branch = $env:SVN_BRANCH
$branch = $branch -replace "/","\"
$env:path = "$env:Path;c:\tools\python2"
$_REPO_LOCATION = [string](Get-Location)
$_FIX_M4A_TIMESTAMPS_SCRIPT_PATH = "$_REPO_LOCATION\build-scripts\fix_m4a_timestamps_in_wwise_file.py"
$_BUNDLE_METADATA_PYTHON_SCRIPT_PATH = "$_REPO_LOCATION\build-scripts\bundle_metadata_products.py"
$_BUNDLE_ASSETS_PYTHON_SCRIPT_PATH = "$_REPO_LOCATION\build-scripts\bundle_soundbank_products.py"
$_ORGANIZE_ASSETS_PYTHON_SCRIPT_PATH = "$_REPO_LOCATION\build-scripts\organize_soundbank_products.py"

$_PROJ_DIR = "$_REPO_LOCATION\$branch\VictorAudio"
$_METADATA_DIR = "$_REPO_LOCATION\$branch\metadata"
$_GSB_DIR = "$_PROJ_DIR\GeneratedSoundBanks"


# Chewie App Platforms
$_SOUNDS_DIR_CHEWIE_IOS =       "$_GSB_DIR\Chewie_iOS"
$_SOUNDS_DIR_CHEWIE_ANDROID =   "$_GSB_DIR\Chewie_Android"

# Victor Robot App Platforms
$_SOUNDS_DIR_VICTOR_LINUX =     "$_GSB_DIR\Victor_Linux"

# Development Victor & Chewie Platform
$_SOUNDS_DIR_DEV_MAC =          "$_GSB_DIR\Dev_Mac"


# Asset Dirs
$_ASSETS_DIR = "assets"
$_TMP_OUTPUT_DIR_CHEWIE_IOS =       "$_ASSETS_DIR\chewie_ios"
$_TMP_OUTPUT_DIR_CHEWIE_ANDROID =   "$_ASSETS_DIR\chewie_android"
$_TMP_OUTPUT_DIR_VICTOR_LINUX =     "$_ASSETS_DIR\victor_linux"
$_TMP_OUTPUT_DIR_DEV_MAC =          "$_ASSETS_DIR\dev_mac"


# Metadata
$_APP_METADATA = "$_ASSETS_DIR\metadata"

$_SKU_DIR = "chewie_app"
$_CHEWIE_BANK_LIST_PATH = "$_METADATA_DIR\chewie-banks-list.json"
$_CHEWIE_IOS = "$_ASSETS_DIR\$_SKU_DIR\chewie_ios"
$_CHEWIE_ANDROID = "$_ASSETS_DIR\$_SKU_DIR\chewie_android"

$_SKU_DIR = "victor_robot"
$_VICTOR_BANK_LIST_PATH = "$_METADATA_DIR\victor-banks-list.json"
$_VICTOR_LINUX = "$_ASSETS_DIR\$_SKU_DIR\victor_linux"

# NOTE: We are currently only using Mac platform for Webots simulator
$_VICTOR_DEV_MAC = "$_ASSETS_DIR\$_SKU_DIR\dev_mac"


if (Test-Path $_ASSETS_DIR) {
    Remove-Item $_ASSETS_DIR -recurse
}

# Make Dirs
mkdir $_TMP_OUTPUT_DIR_CHEWIE_IOS
mkdir $_TMP_OUTPUT_DIR_CHEWIE_ANDROID
mkdir $_TMP_OUTPUT_DIR_VICTOR_LINUX
mkdir $_TMP_OUTPUT_DIR_DEV_MAC

mkdir $_APP_METADATA
mkdir $_CHEWIE_IOS
mkdir $_CHEWIE_ANDROID
mkdir $_VICTOR_LINUX
mkdir $_VICTOR_DEV_MAC

echo "INFO - env.SLACK_CHANNEL`: $slack_channel"
echo $_REPO_LOCATION
echo $_FIX_M4A_TIMESTAMPS_SCRIPT_PATH
echo $_BUNDLE_METADATA_PYTHON_SCRIPT_PATH
echo $_BUNDLE_ASSETS_PYTHON_SCRIPT_PATH
echo $_ORGANIZE_ASSETS_PYTHON_SCRIPT_PATH
echo $_PROJ_DIR
echo $_GSB_DIR
# Platform Paths
echo $_SOUNDS_DIR_CHEWIE_IOS
echo $_SOUNDS_DIR_CHEWIE_ANDROID
echo $_SOUNDS_DIR_VICTOR_LINUX
echo $_SOUNDS_DIR_DEV_MAC
echo $_ASSETS_DIR
echo $_TMP_OUTPUT_DIR_CHEWIE_IOS
echo $_TMP_OUTPUT_DIR_CHEWIE_ANDROID
echo $_TMP_OUTPUT_DIR_VICTOR_LINUX
echo $_TMP_OUTPUT_DIR_DEV_MAC
echo $_APP_METADATA
echo $_CHEWIE_BANK_LIST_PATH
echo $_CHEWIE_IOS
echo $_CHEWIE_ANDROID
echo $_VICTOR_BANK_LIST_PATH
echo $_VICTOR_LINUX
echo $_VICTOR_DEV_MAC

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
  Invoke-RestmethVR -MethVR POST -BVRy ($payload | ConvertTo-Json -Depth 4) -Uri $env:SLACK_TOKEN_URL
}

# send a failure message to slack channel
function CheckExitCode([Int] $exit_code, [String] $error_message) {
    if ($exit_code -ne 0) {
      echo "INFO - Exiting script, there was a problem creating sound bank .zip files , build failed. exitCode $exit_code : $error_message"
      SendFailureSlackMessage "There was a problem creating sound bank .zip files, build failed. exitCode $exit_code : $error_message"
      exit $exit_code
    }
}

# Copy metadata files .txt .json .xml
python $_BUNDLE_METADATA_PYTHON_SCRIPT_PATH $_GSB_DIR $_APP_METADATA
CheckExitCode $lastExitCode

# Fix M4A time stamps so we can compare hash of current assets
# iOS
python $_FIX_M4A_TIMESTAMPS_SCRIPT_PATH $_SOUNDS_DIR_CHEWIE_IOS
CheckExitCode $lastExitCode "There was a problem fixing the timestamps in the .bnk/.wem files in $_SOUNDS_DIR_CHEWIE_IOS."
# Mac
python $_FIX_M4A_TIMESTAMPS_SCRIPT_PATH $_TMP_OUTPUT_DIR_DEV_MAC
CheckExitCode $lastExitCode "There was a problem fixing the timestamps in the .bnk/.wem files in $_TMP_OUTPUT_DIR_DEV_MAC."

# FIXME: We are going to copy Chewie assets into a different SVN repo VIC-2187
# Bundle Chewie assets
#python $_BUNDLE_ASSETS_PYTHON_SCRIPT_PATH $_SOUNDS_DIR_CHEWIE_IOS $_TMP_OUTPUT_DIR_CHEWIE_IOS
#CheckExitCode $lastExitCode

#python $_BUNDLE_ASSETS_PYTHON_SCRIPT_PATH $_SOUNDS_DIR_CHEWIE_ANDROID $_TMP_OUTPUT_DIR_CHEWIE_ANDROID
#CheckExitCode $lastExitCode

# Bundle Victor assets
python $_BUNDLE_ASSETS_PYTHON_SCRIPT_PATH $_SOUNDS_DIR_VICTOR_LINUX $_TMP_OUTPUT_DIR_VICTOR_LINUX
CheckExitCode $lastExitCode

# Bundle DEV Victor assets
python $_BUNDLE_ASSETS_PYTHON_SCRIPT_PATH $_SOUNDS_DIR_DEV_MAC $_TMP_OUTPUT_DIR_DEV_MAC
CheckExitCode $lastExitCode


# Organize sound bank assets
# Chewie App - iOS & Android
#python $_ORGANIZE_ASSETS_PYTHON_SCRIPT_PATH $_TMP_OUTPUT_DIR_CHEWIE_IOS $_CHEWIE_IOS $_CHEWIE_BANK_LIST_PATH
#CheckExitCode $lastExitCode
#python $_ORGANIZE_ASSETS_PYTHON_SCRIPT_PATH $_TMP_OUTPUT_DIR_CHEWIE_ANDROID $_CHEWIE_ANDROID $_CHEWIE_BANK_LIST_PATH
#CheckExitCode $lastExitCode

# Victor - Robot
python $_ORGANIZE_ASSETS_PYTHON_SCRIPT_PATH $_TMP_OUTPUT_DIR_VICTOR_LINUX $_VICTOR_LINUX $_VICTOR_BANK_LIST_PATH --unzip-bundles
CheckExitCode $lastExitCode

# Victor - Dev-Webots
python $_ORGANIZE_ASSETS_PYTHON_SCRIPT_PATH $_TMP_OUTPUT_DIR_DEV_MAC $_VICTOR_DEV_MAC $_VICTOR_BANK_LIST_PATH --unzip-bundles
CheckExitCode $lastExitCode


# Delete temp copy
if (Test-Path $_ASSETS_DIR) {
    Remove-Item $_TMP_OUTPUT_DIR_CHEWIE_IOS -recurse
    Remove-Item $_TMP_OUTPUT_DIR_CHEWIE_ANDROID -recurse
    Remove-Item $_TMP_OUTPUT_DIR_VICTOR_LINUX -recurse
    Remove-Item $_TMP_OUTPUT_DIR_DEV_MAC -recurse
}

exit 0
