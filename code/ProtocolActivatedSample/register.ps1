# Find the path to the built exe under the script's directory, then
# write a custom protocol registration for "custom-uri-launch-sample" into
# the registry for the user.
$scriptPath = Split-Path -Parent $MyInvocation.MyCommand.Path
$exePath = Get-ChildItem $scriptPath -Filter "ProtocolActivatedSample.exe" -Recurse | Select-Object -First 1 -ExpandProperty FullName
$protocolName = "custom-uri-launch-sample"
$protocolKey = "HKCU\Software\Classes\$protocolName"
reg.exe add $protocolKey /f /v "URL Protocol" /t REG_SZ /d ""
reg.exe add "$protocolKey\shell\open\command" /f /ve /t REG_SZ /d "`"$exePath`" `"%1`""
write-host "Registered $($exePath) for protocol $protocolName"
