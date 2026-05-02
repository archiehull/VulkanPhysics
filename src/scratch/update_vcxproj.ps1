$proj = "..\VulkanPhysics.vcxproj"
$content = Get-Content -Raw $proj
$content = $content -replace "<AdditionalIncludeDirectories>", "<AdditionalIncludeDirectories>`$(SolutionDir)Additional Libraries\flatbuffers\include;"

if ($content -notmatch 'FlatBufferSceneLoader\.cpp') {
    $content = $content -replace "</ClCompile>", "</ClCompile>`n    <ClCompile Include=`"src\core\FlatBufferSceneLoader.cpp`" />"
}
if ($content -notmatch 'FlatBufferSceneLoader\.h') {
    $content = $content -replace "</ClInclude>", "</ClInclude>`n    <ClInclude Include=`"src\core\FlatBufferSceneLoader.h`" />"
}
if ($content -notmatch 'scene_generated\.h') {
    $content = $content -replace "</ClInclude>", "</ClInclude>`n    <ClInclude Include=`"src\scene_generated.h`" />"
}
[IO.File]::WriteAllText($proj, $content)

$filt = "..\VulkanPhysics.vcxproj.filters"
$contentF = Get-Content -Raw $filt

if ($contentF -notmatch 'FlatBufferSceneLoader\.cpp') {
    $contentF = $contentF -replace "</ClCompile>", "</ClCompile>`n    <ClCompile Include=`"src\core\FlatBufferSceneLoader.cpp`">`n      <Filter>Source Files</Filter>`n    </ClCompile>"
}
if ($contentF -notmatch 'FlatBufferSceneLoader\.h') {
    $contentF = $contentF -replace "</ClInclude>", "</ClInclude>`n    <ClInclude Include=`"src\core\FlatBufferSceneLoader.h`">`n      <Filter>Header Files</Filter>`n    </ClInclude>"
}
if ($contentF -notmatch 'scene_generated\.h') {
    $contentF = $contentF -replace "</ClInclude>", "</ClInclude>`n    <ClInclude Include=`"src\scene_generated.h`">`n      <Filter>Header Files</Filter>`n    </ClInclude>"
}
[IO.File]::WriteAllText($filt, $contentF)
