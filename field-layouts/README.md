# Bundled field layouts

AprilTag field layouts shipped with LumenVision, copied unchanged from WPILib's `apriltag-java` 2026.2.1 (BSD licence, see
`robot/WPILib-License.md`). The server lists them next to uploaded layouts (`GET /api/fieldLayouts`) so a pipeline can pick the
season's field without uploading a file; Server.csproj publishes them to `fieldLayouts/bundled/`. Add a new season by dropping its
JSON here and giving it a display name in `Server/FieldLayoutCatalog.cs`.
