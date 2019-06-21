workflow "do build" {
  on = "push"
  resolves = ["build"]
}

action "build" {
  uses = "codehz/alpine-builder/action@866337e9be0f671bda9d2610f18fdd0d75a051db"
  args = "-DBUILD_STATIC_STONECTL=ON"
}
