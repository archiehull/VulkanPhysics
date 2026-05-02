import sys

def modify_vcxproj():
    proj_path = r"..\VulkanPhysics.vcxproj"
    with open(proj_path, 'r', encoding='utf-8') as f:
        content = f.read()

    # 1. Add AdditionalIncludeDirectories
    inc_dir = r"$(SolutionDir)Additional Libraries\flatbuffers\include;"
    content = content.replace("<AdditionalIncludeDirectories>", f"<AdditionalIncludeDirectories>{inc_dir}")

    # 2. Add ClCompile
    cl_compile = r'<ClCompile Include="src\core\FlatBufferSceneLoader.cpp" />'
    if cl_compile not in content:
        content = content.replace('</ClCompile>', f'</ClCompile>\n    {cl_compile}')

    # 3. Add ClInclude
    cl_include = r'<ClInclude Include="src\core\FlatBufferSceneLoader.h" />'
    if cl_include not in content:
        content = content.replace('</ClInclude>', f'</ClInclude>\n    {cl_include}')

    # Also add scene_generated.h
    scene_gen_include = r'<ClInclude Include="src\scene_generated.h" />'
    if scene_gen_include not in content:
        content = content.replace('</ClInclude>', f'</ClInclude>\n    {scene_gen_include}')

    with open(proj_path, 'w', encoding='utf-8') as f:
        f.write(content)

def modify_filters():
    filt_path = r"..\VulkanPhysics.vcxproj.filters"
    with open(filt_path, 'r', encoding='utf-8') as f:
        content = f.read()

    cl_compile = r'''    <ClCompile Include="src\core\FlatBufferSceneLoader.cpp">
      <Filter>Source Files</Filter>
    </ClCompile>'''
    
    if "FlatBufferSceneLoader.cpp" not in content:
        content = content.replace('</ClCompile>', f'</ClCompile>\n{cl_compile}')

    cl_include = r'''    <ClInclude Include="src\core\FlatBufferSceneLoader.h">
      <Filter>Header Files</Filter>
    </ClInclude>'''
    
    if "FlatBufferSceneLoader.h" not in content:
        content = content.replace('</ClInclude>', f'</ClInclude>\n{cl_include}')

    scene_gen_include = r'''    <ClInclude Include="src\scene_generated.h">
      <Filter>Header Files</Filter>
    </ClInclude>'''
    
    if "scene_generated.h" not in content:
        content = content.replace('</ClInclude>', f'</ClInclude>\n{scene_gen_include}')

    with open(filt_path, 'w', encoding='utf-8') as f:
        f.write(content)

if __name__ == '__main__':
    modify_vcxproj()
    modify_filters()
    print("Done")
