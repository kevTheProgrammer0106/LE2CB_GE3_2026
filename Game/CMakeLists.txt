# Game = このゲーム固有のコード(シーン・アセット一覧・エントリーポイント)
file(GLOB GAME_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/*.h")

add_executable(Game WIN32 ${GAME_SOURCES})
target_link_libraries(Game PRIVATE Engine)

# 実行時の作業フォルダ = リポジトリの直下(Engine/Shaders と resources/ がここから見える)
set_target_properties(Game PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# exeを直接起動するとき用に、シェーダーとresourcesをexeの隣へコピーする
add_custom_command(TARGET Game POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/Engine/Shaders" "$<TARGET_FILE_DIR:Game>/Engine/Shaders"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/resources" "$<TARGET_FILE_DIR:Game>/resources")
