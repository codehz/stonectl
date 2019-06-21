workflow "do build" {
  on = "push"
  resolves = ["release"]
}

action "build" {
  uses = "codehz/alpine-builder/action@ed273290a31e8c8d927d5b2b51fb58bafb661848"
  args = "-DBUILD_STATIC_STONECTL=ON"
}

action "release" {
  uses = "JasonEtco/upload-to-release@master"
  needs = ["build"]
  args = "install/bin/stonectl application/x-executable"
  secrets = ["GITHUB_TOKEN"]
}
