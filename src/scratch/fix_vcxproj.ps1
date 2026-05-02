$proj = "..\VulkanPhysics.vcxproj"
$content = Get-Content -Raw $proj
$content = $content -replace "<AdditionalIncludeDirectories>", "<AdditionalIncludeDirectories>`$(SolutionDir)Additional Libraries\flatbuffers\include;"

if ($content -notmatch 'FlatBufferSceneLoader\.cpp') {
    $content = $content -replace '<ClCompile Include="src\\main\.cpp" />', "<ClCompile Include=`"src\main.cpp`" />`n    <ClCompile Include=`"src\core\FlatBufferSceneLoader.cpp`" />"
}
if ($content -notmatch 'FlatBufferSceneLoader\.h') {
    $content = $content -replace '<ClInclude Include="src\\core\\Application\.h" />', "<ClInclude Include=`"src\core\Application.h`" />`n    <ClInclude Include=`"src\core\FlatBufferSceneLoader.h`" />"
}
if ($content -notmatch 'scene_generated\.h') {
    $content = $content -replace '<ClInclude Include="src\\core\\Application\.h" />', "<ClInclude Include=`"src\core\Application.h`" />`n    <ClInclude Include=`"src\scene_generated.h`" />"
}
[IO.File]::WriteAllText($proj, $content)

$filt = "..\VulkanPhysics.vcxproj.filters"
$contentF = Get-Content -Raw $filt

if ($contentF -notmatch 'FlatBufferSceneLoader\.cpp') {
    $contentF = $contentF -replace '<ClCompile Include="src\\main\.cpp">', "<ClCompile Include=`"src\core\FlatBufferSceneLoader.cpp`">`n      <Filter>Source Files</Filter>`n    </ClCompile>`n    <ClCompile Include=`"src\main.cpp`">"
}
if ($contentF -notmatch 'FlatBufferSceneLoader\.h') {
    $contentF = $contentF -replace '<ClInclude Include="src\\core\\Application\.h">', "<ClInclude Include=`"src\core\FlatBufferSceneLoader.h`">`n      <Filter>Header Files</Filter>`n    </ClInclude>`n    <ClInclude Include=`"src\core\Application.h`">"
}
if ($contentF -notmatch 'scene_generated\.h') {
    $contentF = $contentF -replace '<ClInclude Include="src\\core\\Application\.h">', "<ClInclude Include=`"src\scene_generated.h`">`n      <Filter>Header Files</Filter>`n    </ClInclude>`n    <ClInclude Include=`"src\core\Application.h`">"
}
[IO.File]::WriteAllText($filt, $contentF)
