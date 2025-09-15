#include "compiler/semantic_analyzer.h"
#include <iostream>
#include <sstream>
#include <cctype>
#include "compiler/catalog.h"
#include "compiler/parser.h"


// 辅助函数：检查值的类型是否匹配
void SemanticAnalyzer::checkValueType(const std::string& value, DataType expectedType) {
    if (expectedType == DataType::INT) {
        try {
            std::stoi(value);
        } catch (...) {
            throw std::runtime_error("Semantic Error: Type mismatch. Expected INT, but got '" + value + "'.");
        }
    } else if (expectedType == DataType::DOUBLE) {
        try {
            std::stod(value);
        } catch (...) {
            throw std::runtime_error("Semantic Error: Type mismatch. Expected DOUBLE, but got '" + value + "'.");
        }
    }
    // 对于 VARCHAR 或 STRING，任何值都可能是合法的，这里不做严格检查
}


SemanticAnalyzer::SemanticAnalyzer(Catalog& catalog)
    : catalog_(catalog) {}

// 主分析函数
void SemanticAnalyzer::analyze(const std::unique_ptr<ASTNode>& ast, const std::vector<Token>& tokens) {
    if (!ast) {
        throw std::runtime_error("Semantic analysis error: AST is empty.");
    }
    
    std::cout << "Starting semantic analysis..." << std::endl;
    try {
        if (auto selectStmt = dynamic_cast<SelectStatement*>(ast.get())) {
            visit(selectStmt, tokens);
        } else if (auto createTableStmt = dynamic_cast<CreateTableStatement*>(ast.get())) {
            visit(createTableStmt, tokens);
        } else if (auto insertStmt = dynamic_cast<InsertStatement*>(ast.get())) {
            visit(insertStmt, tokens);
        } else if (auto deleteStmt = dynamic_cast<DeleteStatement*>(ast.get())) {
            visit(deleteStmt, tokens);
        } else if (auto updateStmt = dynamic_cast<UpdateStatement*>(ast.get())) {
            visit(updateStmt, tokens);
        } else if (auto createIndexStmt = dynamic_cast<CreateIndexStatement*>(ast.get())) {
            visit(createIndexStmt, tokens);
        } else {
            throw std::runtime_error("Semantic analysis error: Unsupported AST node type.");
        }
        std::cout << "Semantic analysis successful." << std::endl;
    } catch (const std::runtime_error& e) {
        throw;
    }
}

// 访问 SELECT 语句节点
void SemanticAnalyzer::visit(SelectStatement* node, const std::vector<Token>& tokens) {
    if (!catalog_.tableExists(node->fromTable)) {
        reportError("Table '" + node->fromTable + "' does not exist.", node->tableTokenIndex, tokens);
    }
    
    // if (!node->columns.empty() && node->columns[0] != "*") {
    //     checkColumnExistence(node->fromTable, node->columns, tokens);
    // }
    
    // checkWhereClause(dynamic_cast<WhereClause*>(node->whereClause.get()), node->fromTable, tokens);
     if (!node->selectAll) {
        checkColumnExistence(node->fromTable, node->columns, tokens);
    }

    // 检查 WHERE 子句
    if (node->whereClause) {
        checkWhereClause(dynamic_cast<WhereClause*>(node->whereClause.get()), node->fromTable, tokens);
    }
}

// 访问 CREATE TABLE 语句节点
void SemanticAnalyzer::visit(CreateTableStatement* node, const std::vector<Token>& tokens) {
    // 1. 检查表是否已存在
    if (catalog_.tableExists(node->tableName)) {
        reportError("Table '" + node->tableName + "' already exists.", node->tableTokenIndex, tokens);
    }
    
    // 2. 将 ColumnDefinition 转换为 ColumnMetadata
    std::vector<ColumnMetadata> newColumns;
    for (const auto& colDef : node->columns) {
        // 【修改】使用 Catalog 类的静态成员函数 stringToDataType
        DataType type = Catalog::stringToDataType(colDef.type);
        if (type == DataType::UNKNOWN) {
            reportError("Unsupported data type '" + colDef.type + "' for column '" + colDef.name + "'.", node->tableTokenIndex, tokens);
        }
        // 【修改】在创建 ColumnMetadata 时，传递 length 字段
        newColumns.push_back({colDef.name, type, colDef.length, colDef.constraints});
    }

    // 3. 注册新表到 Catalog
    catalog_.addTable(node->tableName, newColumns);
}

// 访问 INSERT 语句节点
void SemanticAnalyzer::visit(InsertStatement* node, const std::vector<Token>& tokens) {
    // 1. 检查表是否存在
    if (!catalog_.tableExists(node->tableName)) {
        reportError("Table '" + node->tableName + "' does not exist.", node->tableTokenIndex, tokens);
    }

    // 2. 获取表模式
    const auto& schema = catalog_.getTableSchema(node->tableName);
    
    // 3. 检查值数量是否与列数匹配
    if (schema.columns.size() != node->values.size()) {
        std::stringstream ss;
        ss << "Semantic Error: Number of values (" << node->values.size() 
           << ") does not match the number of columns (" << schema.columns.size() 
           << ") in table '" << node->tableName << "'.";
        reportError(ss.str(), node->tableTokenIndex, tokens);
    }

    // 4. 检查每个值的数据类型是否与列类型匹配
    for (size_t i = 0; i < schema.columns.size(); ++i) {
        const auto& column = schema.columns[i];
        const auto& value = node->values[i];
        checkValueType(value, column.type);
    }
    
    std::cout << "Semantic analysis for INSERT statement passed." << std::endl;
}

// 访问 DELETE 语句节点
void SemanticAnalyzer::visit(DeleteStatement* node, const std::vector<Token>& tokens) {
    if (!catalog_.tableExists(node->tableName)) {
        reportError("Table '" + node->tableName + "' does not exist.", node->tableTokenIndex, tokens);
    }
    checkWhereClause(dynamic_cast<WhereClause*>(node->whereClause.get()), node->tableName, tokens);
}

// 访问 UPDATE 语句节点
void SemanticAnalyzer::visit(UpdateStatement* node, const std::vector<Token>& tokens) {
    if (!catalog_.tableExists(node->tableName)) {
        reportError("Table '" + node->tableName + "' does not exist.", node->tableTokenIndex, tokens);
    }
    
    // 检查 SET 子句中的列是否存在且类型匹配
    for (const auto& pair : node->assignments) {
        const std::string& column = pair.first;
        const std::string& value = pair.second;
        
        if (!catalog_.columnExists(node->tableName, column)) {
            reportError("Column '" + column + "' does not exist in table '" + node->tableName + "'.", node->tableTokenIndex, tokens); // Simplified token index
        }
        
        DataType expectedType = catalog_.getColumnType(node->tableName, column);
        checkValueType(value, expectedType);
    }
    
    checkWhereClause(dynamic_cast<WhereClause*>(node->whereClause.get()), node->tableName, tokens);
}

// 访问 CREATE INDEX 语句节点
void SemanticAnalyzer::visit(CreateIndexStatement* node, const std::vector<Token>& tokens) {
    std::cout << "Analyzing CREATE INDEX statement..." << std::endl;

    // 1. 检查表是否存在
    if (!catalog_.tableExists(node->tableName)) {
        reportError("Table '" + node->tableName + "' does not exist.", 0, tokens);
    }

    // 2. 检查索引名称是否已存在
    if (catalog_.indexExists(node->indexName)) {
        reportError("Index '" + node->indexName + "' already exists.", 0, tokens);
    }

    // 3. 检查列是否存在于表中
    if (!catalog_.columnExists(node->tableName, node->columnName)) {
        reportError("Column '" + node->columnName + "' does not exist in table '" + node->tableName + "'.", 0, tokens);
    }

    // 假设存在一个向 Catalog 添加索引的方法
    catalog_.addIndex(node->indexName, node->tableName, node->columnName);

    std::cout << "CREATE INDEX statement passed semantic checks." << std::endl;
}

// 检查列是否存在
void SemanticAnalyzer::checkColumnExistence(const std::string& tableName, const std::vector<std::string>& columns, const std::vector<Token>& tokens) {
    for (const auto& column : columns) {
        if (!catalog_.columnExists(tableName, column)) {
            reportError("Column '" + column + "' does not exist in table '" + tableName + "'.", 0, tokens); 
        }
    }
}

// 检查 WHERE 子句
void SemanticAnalyzer::checkWhereClause(WhereClause* whereClause, const std::string& tableName, const std::vector<Token>& tokens) {
    if (!whereClause) return;

    // 这是一个简化版的 WHERE 子句检查
    // 【修改】使用 ast.h 中定义的 WhereClause->condition 来解析
    std::stringstream ss(whereClause->condition);
    std::string column, op, value;
    ss >> column >> op >> value;

    if (!catalog_.columnExists(tableName, column)) {
        reportError("Column '" + column + "' in WHERE clause does not exist in table '" + tableName + "'.", whereClause->tokenIndex, tokens);
    }
}

// 报告错误
void SemanticAnalyzer::reportError(const std::string& message, size_t tokenIndex, const std::vector<Token>& tokens) {
    size_t line = 0;
    size_t column = 0;
    if (tokenIndex < tokens.size()) {
        line = tokens[tokenIndex].line;
        column = tokens[tokenIndex].column;
    }
    
    std::stringstream ss;
    ss << "Semantic Error at line " << line << ", column " << column << ": " << message;
    throw std::runtime_error(ss.str());
}