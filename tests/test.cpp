#include <gtest/gtest.h>
#include <fstream>
#include <sstream>
#include <string>
#include <cstdlib>

// Helper function to run refactoring tool on a file and return the result
std::string runRefactorOnFile(const std::string& code) {
    // Create a temporary file
    std::string tempFileName = "/tmp/test_unit_temp.cpp";
    std::ofstream tempFile(tempFileName);
    tempFile << code;
    tempFile.close();

    // Run the refactor tool on the temporary file
    std::string cmd = "./refactor_tool " + tempFileName + " --";
    int result = std::system(cmd.c_str());

    // Read the modified file back
    std::ifstream modifiedFile(tempFileName);
    std::stringstream buffer;
    buffer << modifiedFile.rdbuf();
    std::string resultStr = buffer.str();
    modifiedFile.close();

    // Clean up
    std::remove(tempFileName.c_str());

    return resultStr;
}

// Test 1: Virtual destructor addition - basic case
TEST(VirtualDestructorTest, AddsVirtualToNonVirtualDtorWithDerived) {
    std::string input = R"(class Base {
public:
    ~Base() {}
};
class Derived : public Base {};
)";

    std::string result = runRefactorOnFile(input);

    // Should add virtual to the destructor
    EXPECT_NE(result.find("virtual ~Base()"), std::string::npos);
}

// Test 2: Virtual destructor addition - already virtual (should not change)
TEST(VirtualDestructorTest, DoesNotChangeAlreadyVirtualDtor) {
    std::string input = R"(class Base {
public:
    virtual ~Base() {}
};
class Derived : public Base {};
)";

    std::string result = runRefactorOnFile(input);

    // Should not add another virtual
    int virtualCount = 0;
    size_t pos = 0;
    while ((pos = result.find("virtual", pos)) != std::string::npos) {
        virtualCount++;
        pos += 7; // length of "virtual"
    }
    EXPECT_EQ(virtualCount, 1); // Should have only the original virtual
}

// Test 3: Override addition - basic case
TEST(OverrideTest, AddsOverrideToOverridingMethod) {
    std::string input = R"(class Base {
public:
    virtual void func() {}
};
class Derived : public Base {
public:
    void func() {}
};)";

    std::string result = runRefactorOnFile(input);

    // Should add override to the overriding method
    EXPECT_NE(result.find("func() override"), std::string::npos);
}

// Test 4: Override addition - already has override (should not change)
TEST(OverrideTest, DoesNotChangeAlreadyOverriddenMethod) {
    std::string input = R"(class Base {
public:
    virtual void func() {}
};
class Derived : public Base {
public:
    void func() override {}
};)";

    std::string result = runRefactorOnFile(input);

    // Should not add another override
    int overrideCount = 0;
    size_t pos = 0;
    while ((pos = result.find("override", pos)) != std::string::npos) {
        overrideCount++;
        pos += 8; // length of "override"
    }
    EXPECT_EQ(overrideCount, 1); // Should have only the original override
}

// Test 5: Range-for reference addition - basic case
TEST(RangeForTest, AddsReferenceToConstAuto) {
    std::string input = R"(#include <vector>
struct CustomType {
    int id;
};
void test() {
    std::vector<CustomType> v = {{1}, {2}};
    for (const auto x : v) {
        // do something
    }
}
)";

    std::string result = runRefactorOnFile(input);

    // Should add & to the variable declaration
    EXPECT_NE(result.find("const auto&"), std::string::npos);
}

// Test 6: Range-for reference addition - already has reference (should not change)
TEST(RangeForTest, DoesNotChangeAlreadyReferencedVariable) {
    std::string input = R"(#include <vector>
void test() {
    std::vector<int> v = {1, 2, 3};
    for (const auto& x : v) {
        // do something
    }
}
)";

    std::string result = runRefactorOnFile(input);

    // Should not change anything
    EXPECT_NE(result.find("const auto& x"), std::string::npos);
}

// Test 7: False positive prevention - class without derived classes (should not add virtual)
TEST(FalsePositiveTest, DoesNotAddVirtualToStandaloneClass) {
    std::string input = R"(class Standalone {
public:
    ~Standalone() {}
};
)";

    std::string result = runRefactorOnFile(input);

    // Should not add virtual to a class without derived classes
    EXPECT_EQ(result.find("virtual ~Standalone()"), std::string::npos);
}

// Test 8: False positive prevention - fundamental types in range-for (should not change)
TEST(FalsePositiveTest, DoesNotChangeFundamentalTypesInRangeFor) {
    std::string input = R"(#include <vector>
void test() {
    std::vector<int> v = {1, 2, 3};
    for (const int x : v) {
        // do something
    }
}
)";

    std::string result = runRefactorOnFile(input);

    // Should not change fundamental types
    EXPECT_NE(result.find("const int x"), std::string::npos);
}