// =============================================================================
//  Jenkins CI/CD Pipeline - High-Performance Multi-Threaded Chat Server
//  Declarative Pipeline for Windows Build Agents
// =============================================================================

pipeline {
    agent {
        label 'windows'  // Requires a Windows build agent
    }

    // -------------------------------------------------------------------------
    //  Build Parameters (configurable from Jenkins UI)
    // -------------------------------------------------------------------------
    parameters {
        choice(
            name: 'BUILD_TYPE',
            choices: ['Release', 'Debug', 'RelWithDebInfo'],
            description: 'CMake build type'
        )
        booleanParam(
            name: 'RUN_BENCHMARKS',
            defaultValue: true,
            description: 'Run performance benchmarks after tests'
        )
        booleanParam(
            name: 'USE_CMAKE',
            defaultValue: true,
            description: 'Use CMake build system (false = use MinGW batch script)'
        )
    }

    // -------------------------------------------------------------------------
    //  Environment Variables
    // -------------------------------------------------------------------------
    environment {
        BUILD_DIR       = 'build'
        CMAKE_BUILD_TYPE = "${params.BUILD_TYPE ?: 'Release'}"
        // Timestamp for artifact tagging
        BUILD_TIMESTAMP = "${new Date().format('yyyy-MM-dd_HH-mm-ss')}"
    }

    // -------------------------------------------------------------------------
    //  Pipeline Options
    // -------------------------------------------------------------------------
    options {
        timestamps()                      // Prefix console output with timestamps
        timeout(time: 30, unit: 'MINUTES') // Global timeout
        buildDiscarder(logRotator(
            numToKeepStr: '10',           // Keep last 10 builds
            artifactNumToKeepStr: '5'     // Keep artifacts for last 5 builds
        ))
        disableConcurrentBuilds()          // Prevent parallel builds
    }

    // -------------------------------------------------------------------------
    //  Pipeline Stages
    // -------------------------------------------------------------------------
    stages {

        // =====================================================================
        //  Stage 1: Checkout & Environment Info
        // =====================================================================
        stage('Checkout') {
            steps {
                cleanWs()           // Start with a clean workspace
                checkout scm        // Pull source from SCM (Git)

                echo '============================================='
                echo '  Chat Server CI/CD Pipeline'
                echo '============================================='
                echo "Build Type:     ${CMAKE_BUILD_TYPE}"
                echo "Build Number:   ${env.BUILD_NUMBER}"
                echo "Branch:         ${env.BRANCH_NAME ?: 'N/A'}"
                echo "Commit:         ${env.GIT_COMMIT ?: 'N/A'}"
                echo '============================================='

                // Print environment info for debugging
                bat 'echo === System Info === && systeminfo | findstr /B /C:"OS Name" /C:"OS Version" && echo.'
                bat 'g++ --version 2>nul || echo [INFO] g++ not found'
                bat 'cmake --version 2>nul || echo [INFO] cmake not found'
            }
        }

        // =====================================================================
        //  Stage 2: Build All Targets
        // =====================================================================
        stage('Build') {
            steps {
                script {
                    if (params.USE_CMAKE) {
                        echo '[BUILD] Using CMake build system...'
                        bat """
                            if not exist ${BUILD_DIR} mkdir ${BUILD_DIR}
                            cd ${BUILD_DIR}
                            cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} -DCMAKE_CXX_STANDARD=17
                            cmake --build . --config ${CMAKE_BUILD_TYPE} -j %NUMBER_OF_PROCESSORS%
                        """

                        // Build test and benchmark targets separately (not in CMakeLists.txt)
                        echo '[BUILD] Building test & benchmark targets...'
                        bat """
                            g++ -std=c++17 -O2 -Wall -D_WIN32_WINNT=0x0601 ^
                                -o ${BUILD_DIR}/tests.exe ^
                                tests.cpp thread_pool.cpp ^
                                -lws2_32

                            g++ -std=c++17 -O2 -Wall -D_WIN32_WINNT=0x0601 ^
                                -o ${BUILD_DIR}/benchmark.exe ^
                                benchmark.cpp thread_pool.cpp ^
                                -lws2_32

                            g++ -std=c++17 -O2 -Wall -D_WIN32_WINNT=0x0601 ^
                                -o ${BUILD_DIR}/stress_test.exe ^
                                stress_test.cpp sockutil.cpp ^
                                -lws2_32
                        """
                    } else {
                        echo '[BUILD] Using MinGW batch build script...'
                        bat 'build_mingw.bat'
                    }
                }
            }
        }

        // =====================================================================
        //  Stage 3: Unit Tests
        // =====================================================================
        stage('Unit Tests') {
            steps {
                echo '[TEST] Running unit tests...'

                // Run tests and capture output
                bat """
                    ${BUILD_DIR}\\tests.exe > ${BUILD_DIR}\\test_output.txt 2>&1
                    echo Test exit code: %ERRORLEVEL%
                """

                // Display test results in console
                bat "type ${BUILD_DIR}\\test_output.txt"

                // Convert test output to JUnit XML for Jenkins reporting
                script {
                    generateJUnitReport()
                }
            }
            post {
                always {
                    // Publish JUnit test results
                    junit(
                        testResults: "${BUILD_DIR}/test-results.xml",
                        allowEmptyResults: true
                    )
                    // Archive test output
                    archiveArtifacts(
                        artifacts: "${BUILD_DIR}/test_output.txt",
                        allowEmptyArchive: true
                    )
                }
            }
        }

        // =====================================================================
        //  Stage 4: Performance Benchmarks (Optional)
        // =====================================================================
        stage('Benchmarks') {
            when {
                expression { return params.RUN_BENCHMARKS }
            }
            steps {
                echo '[BENCHMARK] Running performance benchmarks...'

                bat """
                    ${BUILD_DIR}\\benchmark.exe > ${BUILD_DIR}\\benchmark_output.txt 2>&1
                """

                bat "type ${BUILD_DIR}\\benchmark_output.txt"

                // Archive benchmark results
                script {
                    if (fileExists('BENCHMARK_RESULTS.md')) {
                        archiveArtifacts artifacts: 'BENCHMARK_RESULTS.md'
                    }
                }
            }
            post {
                always {
                    archiveArtifacts(
                        artifacts: "${BUILD_DIR}/benchmark_output.txt",
                        allowEmptyArchive: true
                    )
                }
            }
        }

        // =====================================================================
        //  Stage 5: Static Analysis (Code Quality)
        // =====================================================================
        stage('Static Analysis') {
            when {
                expression { return fileExists('cppcheck.exe') || true }
            }
            steps {
                echo '[ANALYSIS] Running static analysis...'
                script {
                    // Try cppcheck if available
                    def cppcheckStatus = bat(
                        script: 'where cppcheck 2>nul',
                        returnStatus: true
                    )
                    if (cppcheckStatus == 0) {
                        bat """
                            cppcheck --enable=all --std=c++17 ^
                                --suppress=missingIncludeSystem ^
                                --xml --xml-version=2 ^
                                --output-file=${BUILD_DIR}/cppcheck-report.xml ^
                                server.cpp client.cpp tests.cpp ^
                                lock_free_queue.h object_pool.h perf_metrics.h ^
                                thread_pool.h thread_pool.cpp ^
                                sockutil.h sockutil.cpp ^
                                iocp_server.h iocp_server.cpp ^
                                connection_manager.h connection_manager.cpp ^
                                chat_room.h chat_room.cpp ^
                                message_store.h message_store.cpp
                        """
                        archiveArtifacts(
                            artifacts: "${BUILD_DIR}/cppcheck-report.xml",
                            allowEmptyArchive: true
                        )
                    } else {
                        echo '[ANALYSIS] cppcheck not found — skipping static analysis.'
                        echo '[ANALYSIS] Install cppcheck for code quality reports.'
                    }
                }
            }
        }

        // =====================================================================
        //  Stage 6: Archive Build Artifacts
        // =====================================================================
        stage('Archive Artifacts') {
            steps {
                echo '[ARCHIVE] Archiving build artifacts...'

                archiveArtifacts(
                    artifacts: "${BUILD_DIR}/**/*.exe",
                    fingerprint: true,
                    onlyIfSuccessful: true
                )

                echo '[ARCHIVE] Build artifacts archived successfully.'
                echo "           Download from: ${env.BUILD_URL}artifact/"
            }
        }
    }

    // -------------------------------------------------------------------------
    //  Post-Build Actions
    // -------------------------------------------------------------------------
    post {
        always {
            echo '============================================='
            echo '  Pipeline Complete'
            echo '============================================='
            echo "Result:       ${currentBuild.currentResult}"
            echo "Duration:     ${currentBuild.durationString}"
            echo "Build URL:    ${env.BUILD_URL}"
            echo '============================================='

            // Clean up temporary files
            bat "del /Q ${BUILD_DIR}\\*.obj 2>nul & exit /b 0"
        }

        success {
            echo '==========================================='
            echo '  ✅ BUILD SUCCESSFUL'
            echo '==========================================='

            // -----------------------------------------------------------------
            // Uncomment below to enable email notifications:
            // -----------------------------------------------------------------
            // mail to: 'your-email@example.com',
            //      subject: "✅ Chat Server Build #${env.BUILD_NUMBER} - SUCCESS",
            //      body: """Build succeeded!
            //          |Branch: ${env.BRANCH_NAME}
            //          |Build: ${env.BUILD_URL}
            //          |Duration: ${currentBuild.durationString}
            //      """.stripMargin()

            // -----------------------------------------------------------------
            // Uncomment below to enable Slack notifications:
            // -----------------------------------------------------------------
            // slackSend(
            //     color: 'good',
            //     channel: '#ci-builds',
            //     message: "✅ Chat Server Build #${env.BUILD_NUMBER} PASSED\n${env.BUILD_URL}"
            // )
        }

        failure {
            echo '==========================================='
            echo '  ❌ BUILD FAILED'
            echo '==========================================='

            // -----------------------------------------------------------------
            // Uncomment below to enable email notifications:
            // -----------------------------------------------------------------
            // mail to: 'your-email@example.com',
            //      subject: "❌ Chat Server Build #${env.BUILD_NUMBER} - FAILED",
            //      body: """Build failed!
            //          |Branch: ${env.BRANCH_NAME}
            //          |Build: ${env.BUILD_URL}
            //          |Check the console output for details.
            //      """.stripMargin()

            // -----------------------------------------------------------------
            // Uncomment below to enable Slack notifications:
            // -----------------------------------------------------------------
            // slackSend(
            //     color: 'danger',
            //     channel: '#ci-builds',
            //     message: "❌ Chat Server Build #${env.BUILD_NUMBER} FAILED\n${env.BUILD_URL}"
            // )
        }

        unstable {
            echo '==========================================='
            echo '  ⚠️  BUILD UNSTABLE (some tests failed)'
            echo '==========================================='
        }
    }
}

// =============================================================================
//  Helper Functions
// =============================================================================

/**
 * Generates a JUnit-compatible XML report from the test output.
 * Parses the custom test framework output ([PASS]/[FAIL] lines) and
 * converts them into JUnit XML format for Jenkins test reporting.
 */
def generateJUnitReport() {
    def testOutput = readFile("${BUILD_DIR}/test_output.txt")
    def lines = testOutput.split('\n')

    def testCases = []
    def totalTests = 0
    def failures = 0

    for (line in lines) {
        line = line.trim()
        if (line.contains('[PASS]')) {
            def testName = line.replaceAll(/.*\[PASS\]\s*/, '').trim()
            testCases.add("<testcase classname=\"ChatServer\" name=\"${escapeXml(testName)}\" time=\"0.001\"/>")
            totalTests++
        } else if (line.contains('[FAIL]')) {
            def testName = line.replaceAll(/.*\[FAIL\]\s*/, '').trim()
            testCases.add("""<testcase classname="ChatServer" name="${escapeXml(testName)}" time="0.001">
    <failure message="Test failed">${escapeXml(testName)}</failure>
</testcase>""")
            totalTests++
            failures++
        }
    }

    def junitXml = """<?xml version="1.0" encoding="UTF-8"?>
<testsuite name="ChatServerTests" tests="${totalTests}" failures="${failures}" errors="0" time="0.0">
${testCases.join('\n')}
</testsuite>"""

    writeFile file: "${BUILD_DIR}/test-results.xml", text: junitXml
    echo "[TEST] Generated JUnit report: ${totalTests} tests, ${failures} failures"
}

/**
 * Escapes special XML characters in a string.
 */
def escapeXml(String text) {
    return text.replace('&', '&amp;')
               .replace('<', '&lt;')
               .replace('>', '&gt;')
               .replace('"', '&quot;')
               .replace("'", '&apos;')
}
