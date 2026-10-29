// Pipeline de compilacion y empaquetado de OCRX en un agente Windows con VS 2022 Build Tools, CMake y vcpkg.
pipeline {
    agent { label 'windows' }
    options { timestamps(); buildDiscarder(logRotator(numToKeepStr: '10')) }
    environment { VCPKG_ROOT = 'C:\\vcpkg' }
    stages {
        stage('Dependencias') { steps { powershell 'powershell -ExecutionPolicy Bypass -File scripts\\descargar-deps.ps1' } }
        stage('Configurar')   { steps { powershell 'cmake --preset windows-x64' } }
        stage('Compilar')     { steps { powershell 'cmake --build --preset windows-x64' } }
        stage('Pruebas')      { steps { powershell 'ctest --preset windows-x64 --output-on-failure' } }
        stage('Paquete') {
            steps {
                powershell '''
                  if (Test-Path dist) { Remove-Item -Recurse -Force dist }
                  cmake --install build --config Release --prefix dist\\ocrx
                  Copy-Item README.md dist\\ocrx
                  Compress-Archive -Path dist\\ocrx\\* -DestinationPath dist\\ocrx-win-x64.zip -Force
                '''
                archiveArtifacts artifacts: 'dist/ocrx-win-x64.zip', fingerprint: true
            }
        }
    }
}
