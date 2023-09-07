import sys
import os
import glob
import os.path
import shutil
import subprocess
import xml.etree.ElementTree


def packmanExt():
    if sys.platform == 'win32':
        return 'cmd'
    return 'sh'


def cmakeExt():
    if sys.platform == 'win32':
        return '.exe'
    return ''


def filterPreset(presetName):
    winPresetFilter = ['win','switch','crosscompile']
    if sys.platform == 'win32':        
        if any(presetName.find(elem) != -1 for elem in winPresetFilter):
            return True
    else:        
        if all(presetName.find(elem) == -1 for elem in winPresetFilter):
            return True
    return False

def noPresetProvided():
    global input
    print('Preset parameter required, available presets:')
    presetfiles = []
    for file in glob.glob("buildtools/presets/*.xml"):
        presetfiles.append(file)

    if len(presetfiles) == 0:
        for file in glob.glob("buildtools/presets/public/*.xml"):
            presetfiles.append(file)

    counter = 0
    presetList = []
    for preset in presetfiles:
        if filterPreset(preset):
            presetXml = xml.etree.ElementTree.parse(preset).getroot()
            if(preset.find('user') == -1):
                print('(' + str(counter) + ') ' + presetXml.get('name') +
                    ' <--- ' + presetXml.get('comment'))
                presetList.append(presetXml.get('name'))
            else:
                print('(' + str(counter) + ') ' + presetXml.get('name') +
                    '.user <--- ' + presetXml.get('comment'))
                presetList.append(presetXml.get('name') + '.user')
            counter = counter + 1            
    # Fix Python 2.x.
    try: 
    	input = raw_input
    except NameError: 
    	pass    
    mode = int(eval(input('Enter preset number: ')))
    return presetList[mode]

class CMakePreset:
    presetName = ''
    targetPlatform = ''
    compiler = ''
    cmakeSwitches = []
    cmakeParams = []

    def __init__(self, presetName):
        xmlPath = "buildtools/presets/"+presetName+'.xml'
        if os.path.isfile(xmlPath):
            print('Using preset xml: '+xmlPath)
        else:
            xmlPath = "buildtools/presets/public/"+presetName+'.xml'
            if os.path.isfile(xmlPath):
                print('Using preset xml: '+xmlPath)
            else:
                print('Preset xml file: '+xmlPath+' not found')
                exit()

        # get the xml
        presetNode = xml.etree.ElementTree.parse(xmlPath).getroot()
        self.presetName = presetNode.attrib['name']
        for platform in presetNode.findall('platform'):
            self.targetPlatform = platform.attrib['targetPlatform']
            self.compiler = platform.attrib['compiler']
            print('Target platform: ' + self.targetPlatform +
                  ' using compiler: ' + self.compiler)

        for cmakeSwitch in presetNode.find('CMakeSwitches'):
            cmSwitch = '-D' + \
                cmakeSwitch.attrib['name'] + '=' + \
                cmakeSwitch.attrib['value'].upper()
            self.cmakeSwitches.append(cmSwitch)

        for cmakeParam in presetNode.find('CMakeParams'):
            if cmakeParam.attrib['name'] == 'CMAKE_INSTALL_PREFIX' or cmakeParam.attrib['name'] == 'PX_OUTPUT_LIB_DIR' or cmakeParam.attrib['name'] == 'PX_OUTPUT_EXE_DIR' or cmakeParam.attrib['name'] == 'PX_OUTPUT_DLL_DIR':
                cmParam = '-D' + cmakeParam.attrib['name'] + '=\"' + \
                    os.environ['PHYSX_ROOT_DIR'] + '/' + \
                    cmakeParam.attrib['value'] + '\"'
            else:
                cmParam = '-D' + \
                    cmakeParam.attrib['name'] + '=' + \
                    cmakeParam.attrib['value']
            self.cmakeParams.append(cmParam)
    pass

    def isMultiConfigPlatform(self):
        if self.targetPlatform == 'linux':
            return False
        elif self.targetPlatform == 'linuxAarch64':
            return False
        elif self.targetPlatform == 'psp':
            return False
        return True

    def getCMakeSwitches(self):
        outString = ''
        for cmakeSwitch in self.cmakeSwitches:
            outString += ' ' + cmakeSwitch
            if cmakeSwitch.find('PX_GENERATE_GPU_PROJECTS') != -1:
                if os.environ.get('PM_CUDA_PATH') is not None:
                    outString += ' -DCUDA_TOOLKIT_ROOT_DIR=' + \
                        os.environ['PM_CUDA_PATH']
                if self.compiler == 'vc15':
                    print('VS15CL:' + os.environ['VS150CLPATH'])
                    outString += ' -DCUDA_HOST_COMPILER=' + \
                        os.environ['VS150CLPATH']
                if self.compiler == 'vc16':
                    print('VS16CL:' + os.environ['VS160CLPATH'])
                    outString += ' -DCUDA_HOST_COMPILER=' + \
                        os.environ['VS160CLPATH']
                if self.compiler == 'vc17':
                    print('VS17CL:' + os.environ['VS170CLPATH'])
                    outString += ' -DCUDA_HOST_COMPILER=' + \
                        os.environ['VS170CLPATH']

        return outString

    def getCMakeParams(self):
        outString = ''
        for cmakeParam in self.cmakeParams:
            outString += ' ' + cmakeParam
        return outString

    def getPlatformCMakeParams(self):
        outString = ' '
        if self.compiler == 'vc15':
            outString += '-G \"Visual Studio 15 2017\"'
        elif self.compiler == 'vc16':
            outString += '-G \"Visual Studio 16 2019\"'
        elif self.compiler == 'vc17':
            outString += '-G \"Visual Studio 17 2022\"'
        elif self.compiler == 'xcode':
            outString += '-G Xcode'
        elif self.targetPlatform == 'linux':
            outString += '-G \"Unix Makefiles\"'
        elif self.targetPlatform == 'linuxAarch64':
            outString += '-G \"Unix Makefiles\"'
        elif self.targetPlatform == 'psp':
            outString += '-G \"Unix Makefiles\"'

        if self.targetPlatform == 'win64':
            outString += ' -Ax64'
            outString += ' -DTARGET_BUILD_PLATFORM=windows'
            outString += ' -DPX_OUTPUT_ARCH=x86'
            return outString
        elif self.targetPlatform == 'switch64':
            outString += ' -DTARGET_BUILD_PLATFORM=switch'
            outString += ' -DCMAKE_TOOLCHAIN_FILE=' + \
                os.environ['PHYSX_ROOT_DIR'] + '/compiler/modules/switch/NX64Toolchain.txt'
            outString += ' -DCMAKE_GENERATOR_PLATFORM=NX64'
            outString += ' -DCMAKE_VS_USER_PROPS=' + os.environ['PHYSX_ROOT_DIR'] + '/compiler/modules/switch/Microsoft.Cpp.NX-NXFP2-a64.user.props'
            return outString
        elif self.targetPlatform == 'psp':
            outString += ' -DTARGET_BUILD_PLATFORM=psp'
            return outString
        elif self.targetPlatform == 'linux':
            outString += ' -DTARGET_BUILD_PLATFORM=linux'
            outString += ' -DPX_OUTPUT_ARCH=x86'
            if self.compiler == 'clang-crosscompile':
                outString += ' -DCMAKE_TOOLCHAIN_FILE=' + \
                    os.environ['PHYSX_ROOT_DIR'] + '/compiler/modules/linux/LinuxCrossToolchain.x86_64-unknown-linux-gnu.cmake'
            elif self.compiler == 'clang':
                if os.environ.get('PM_clang_PATH') is not None:
                    outString += ' -DCMAKE_C_COMPILER=' + \
                        os.environ['PM_clang_PATH'] + '/bin/clang'
                    outString += ' -DCMAKE_CXX_COMPILER=' + \
                        os.environ['PM_clang_PATH'] + '/bin/clang++'
                else:
                    outString += ' -DCMAKE_C_COMPILER=clang'
                    outString += ' -DCMAKE_CXX_COMPILER=clang++'
            return outString
        elif self.targetPlatform == 'linuxAarch64':
            outString += ' -DTARGET_BUILD_PLATFORM=linux'
            outString += ' -DPX_OUTPUT_ARCH=arm'
            if self.compiler == 'clang-crosscompile':
                outString += ' -DCMAKE_TOOLCHAIN_FILE=' + \
                    os.environ['PHYSX_ROOT_DIR'] + '/compiler/modules/linux/LinuxCrossToolchain.aarch64-unknown-linux-gnueabihf.cmake'
            elif self.compiler == 'gcc':
                outString += ' -DCMAKE_TOOLCHAIN_FILE=\"' + \
                    os.environ['PHYSX_ROOT_DIR'] + '/compiler/modules/linux/LinuxAarch64.cmake\"'
            return outString
        elif self.targetPlatform == 'mac64':
            outString += ' -DTARGET_BUILD_PLATFORM=mac'
            outString += ' -DPX_OUTPUT_ARCH=x86'
            return outString
        return ''


def getCommonParams():
    outString = '--no-warn-unused-cli'
    outString += ' -DCMAKE_PREFIX_PATH=\"' + os.environ['PHYSX_ROOT_DIR'] + '/compiler/modules\"'
    outString += ' -DPHYSX_ROOT_DIR=\"' + \
        os.environ['PHYSX_ROOT_DIR'] + '\"'
    outString += ' -DPX_OUTPUT_LIB_DIR=\"' + \
        os.environ['PHYSX_ROOT_DIR'] + '\"'
    outString += ' -DPX_OUTPUT_BIN_DIR=\"' + \
        os.environ['PHYSX_ROOT_DIR'] + '\"'
    if os.environ.get('GENERATE_SOURCE_DISTRO') == '1':
        outString += ' -DPX_GENERATE_SOURCE_DISTRO=1'
    return outString

def cleanupCompilerDir(compilerDirName):
    if os.path.exists(compilerDirName):
        if sys.platform == 'win32':
            os.system('rmdir /S /Q ' + compilerDirName)
        else:
            shutil.rmtree(compilerDirName, True)
    if os.path.exists(compilerDirName) == False:
        os.makedirs(compilerDirName)

def presetProvided(pName):
    parsedPreset = CMakePreset(pName)

    print('PM_CMakeModules_PATH: ' + os.environ['PHYSX_ROOT_DIR'] + '/compiler/modules')
    print('PM_PATHS: ' + os.environ['PM_PATHS'])

    if os.environ.get('PM_cmake_PATH') is not None:
        cmakeExec = os.environ['PM_cmake_PATH'] + '/bin/cmake' + cmakeExt()
    elif pName == 'psp':
        cmakeExec = 'psp-cmake'
    else:
        cmakeExec = 'cmake' + cmakeExt()
    print('Cmake: ' + cmakeExec)

    # gather cmake parameters
    cmakeParams = parsedPreset.getPlatformCMakeParams()
    cmakeParams = cmakeParams + ' ' + getCommonParams()
    cmakeParams = cmakeParams + ' ' + parsedPreset.getCMakeSwitches()
    cmakeParams = cmakeParams + ' ' + parsedPreset.getCMakeParams()
    print(cmakeParams)

    if os.path.isfile(os.environ['PHYSX_ROOT_DIR'] + '/compiler/internal/CMakeLists.txt'):
        cmakeMasterDir = 'internal'
    else:
        cmakeMasterDir = 'public'
    if parsedPreset.isMultiConfigPlatform():
        # cleanup and create output directory
        outputDir = os.path.join('compiler', parsedPreset.presetName)
        cleanupCompilerDir(outputDir)

        # run the cmake script
        #print('Cmake params:' + cmakeParams)
        os.chdir(os.path.join(os.environ['PHYSX_ROOT_DIR'], outputDir))
        os.system(cmakeExec + ' \"' +
                  os.environ['PHYSX_ROOT_DIR'] + '/compiler/' + cmakeMasterDir + '\"' + cmakeParams)
        os.chdir(os.environ['PHYSX_ROOT_DIR'])
    else:
        configs = ['debug', 'checked', 'profile', 'release']
        for config in configs:
            # cleanup and create output directory
            outputDir = os.path.join('compiler', parsedPreset.presetName + '-' + config)
            cleanupCompilerDir(outputDir)

            # run the cmake script
            print('Cmake params:' + cmakeParams)
            os.chdir(os.path.join(os.environ['PHYSX_ROOT_DIR'], outputDir))
            print(cmakeExec + ' \"' + os.environ['PHYSX_ROOT_DIR'] + '/compiler/' + cmakeMasterDir + '\"' + cmakeParams + ' -DCMAKE_BUILD_TYPE=' + config)
            os.system(cmakeExec + ' \"' + os.environ['PHYSX_ROOT_DIR'] + '/compiler/' +
                      cmakeMasterDir + '\"' + cmakeParams + ' -DCMAKE_BUILD_TYPE=' + config)
            os.chdir(os.environ['PHYSX_ROOT_DIR'])
    pass


def main():
    if (sys.version_info[0] < 3) or (sys.version_info[0] == 3 and sys.version_info[1] < 5):
        print("You are using Python {}. You must use Python 3.5 and up. Please read README.md for requirements.").format(sys.version)
        exit()
    if len(sys.argv) != 2:
        presetName = noPresetProvided()
        if sys.platform == 'win32':
            print('Running generate_projects.bat ' + presetName)
            cmd = 'generate_projects.bat {}'.format(presetName)
            result = subprocess.run(cmd, cwd=os.environ['PHYSX_ROOT_DIR'], check=True, universal_newlines=True)
            # TODO: catch exception and add capture errors
        else:
            print('Running generate_projects.sh ' + presetName)
            # TODO: once we have Python 3.7.2 for linux, add the text=True instead of universal_newlines 
            cmd = './generate_projects.sh {}'.format(presetName)
            result = subprocess.run(['bash', './generate_projects.sh', presetName], cwd=os.environ['PHYSX_ROOT_DIR'], check=True, universal_newlines=True)
            # TODO: catch exception and add capture errors
    else:
        presetName = sys.argv[1]
        if filterPreset(presetName):
            presetProvided(presetName)
        else:
            print('Preset not supported on this build platform.')

main()
