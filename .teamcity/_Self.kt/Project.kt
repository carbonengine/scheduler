package _Self

import _Self.buildTypes.*
import jetbrains.buildServer.configs.kotlin.*
import jetbrains.buildServer.configs.kotlin.Project
import jetbrains.buildServer.configs.kotlin.vcs.GitVcsRoot


object Project : Project({

    description = "Build / Publish pipeline for https://github.com/ccpgames/carbon-io"

    params {
        param("carbon_ref", "refs/heads/feature/kotlin")
        param("carbon-pipeline-tools-ref", "refs/heads/main")
    }
    
    subProject(Windows.Project)
    subProject(MacOS.Project)

    buildType(PublishToPerforce)
})
