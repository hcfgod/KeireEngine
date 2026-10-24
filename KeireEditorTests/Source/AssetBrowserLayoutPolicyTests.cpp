#include "KeireClient/Editor/AssetBrowserLayoutPolicy.h"

#include <doctest/doctest.h>

TEST_CASE("Asset browser keeps content space when the editor is restored")
{
    CHECK(KeireEditor::UseCompactAssetBrowserChrome(100.0F));
    CHECK(KeireEditor::UseCompactAssetBrowserChrome(172.0F));
    CHECK(KeireEditor::UseCompactAssetBrowserChrome(239.0F));
    CHECK_FALSE(KeireEditor::UseCompactAssetBrowserChrome(240.0F));
    CHECK_FALSE(KeireEditor::UseCompactAssetBrowserChrome(400.0F));
    CHECK(KeireEditor::UseAssetBrowserListForHeight(60.0F, 96.0F));
    CHECK(KeireEditor::UseAssetBrowserListForHeight(143.0F, 96.0F));
    CHECK_FALSE(KeireEditor::UseAssetBrowserListForHeight(144.0F, 96.0F));
    CHECK_FALSE(KeireEditor::UseAssetBrowserListForHeight(300.0F, 160.0F));
}
