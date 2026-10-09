#include "ScriptExpression.h"

#include <QJsonArray>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <vector>

namespace lmms::agent {
namespace {
[[noreturn]] void fail(const QString& message)
{
	throw std::runtime_error(message.toStdString());
}

QJsonValue variable(const QString& path, const QJsonObject& variables)
{
	auto normalized = path;
	normalized.replace('[', '.');
	normalized.remove(']');
	const auto parts = normalized.split('.');
	QJsonValue value(variables);
	for (const auto& part : parts)
	{
		if (value.isObject())
		{
			value = value.toObject().value(part);
		}
		else if (value.isArray())
		{
			bool valid = false;
			const int index = part.toInt(&valid);
			const auto array = value.toArray();
			value = valid && index >= 0 && index < array.size() ? array[index] : QJsonValue(QJsonValue::Undefined);
		}
		else
		{
			value = QJsonValue(QJsonValue::Undefined);
		}
		if (value.isUndefined())
		{
			fail("Unknown script variable or field: $" + path);
		}
	}
	return value;
}

struct Node
{
	QString kind;
	QString text;
	QJsonValue value;
	std::vector<std::unique_ptr<Node>> children;
};

class Parser
{
public:
	explicit Parser(const QString& input)
		: m_input(input)
	{
		if (input.size() > 4096)
		{
			fail("Expressions are limited to 4096 characters.");
		}
		next();
	}

	std::unique_ptr<Node> parse(int precedence = 0, int depth = 0)
	{
		if (depth > 64)
		{
			fail("Expression nesting exceeds 64 levels.");
		}
		auto left = primary(depth + 1);
		while (rank(m_token) > precedence)
		{
			const auto operation = m_token;
			const int level = rank(operation);
			next();
			auto node = make("binary", operation);
			node->children.push_back(std::move(left));
			node->children.push_back(parse(level, depth + 1));
			left = std::move(node);
		}
		return left;
	}

	bool atEnd() const { return m_token.isEmpty(); }

private:
	static int rank(const QString& token)
	{
		if (token == "||")
		{
			return 1;
		}
		if (token == "&&")
		{
			return 2;
		}
		if (token == "==" || token == "!=")
		{
			return 3;
		}
		if (token == "<" || token == ">" || token == "<=" || token == ">=")
		{
			return 4;
		}
		if (token == "+" || token == "-")
		{
			return 5;
		}
		if (token == "*" || token == "/" || token == "%")
		{
			return 6;
		}
		return 0;
	}

	std::unique_ptr<Node> make(const QString& kind, const QString& text = {})
	{
		if (++m_nodes > 1024)
		{
			fail("An expression exceeds the 1024-node limit.");
		}
		auto node = std::make_unique<Node>();
		node->kind = kind;
		node->text = text;
		return node;
	}

	void require(const QString& token)
	{
		if (m_token != token)
		{
			fail("Expected '" + token + "' in expression.");
		}
		next();
	}

	std::unique_ptr<Node> primary(int depth)
	{
		if (depth > 64)
		{
			fail("Expression nesting exceeds 64 levels.");
		}
		if (m_token == "+" || m_token == "-" || m_token == "!")
		{
			auto node = make("unary", m_token);
			next();
			node->children.push_back(primary(depth + 1));
			return node;
		}
		if (m_token == "(")
		{
			next();
			auto node = parse(0, depth + 1);
			require(")");
			return node;
		}
		if (m_token == "[")
		{
			auto node = make("array");
			next();
			if (m_token != "]")
			{
				for (;;)
				{
					node->children.push_back(parse(0, depth + 1));
					if (m_token != ",")
					{
						break;
					}
					next();
				}
			}
			require("]");
			return node;
		}
		if (m_kind == "value")
		{
			auto node = make("value");
			node->value = m_value;
			next();
			return node;
		}
		if (m_kind == "variable")
		{
			auto node = make("variable", m_token.mid(1));
			next();
			while (m_token == "[")
			{
				next();
				auto indexed = make("index");
				indexed->children.push_back(std::move(node));
				indexed->children.push_back(parse(0, depth + 1));
				require("]");
				node = std::move(indexed);
			}
			return node;
		}
		if (m_kind == "name")
		{
			const auto name = m_token;
			next();
			if (name == "true" || name == "false")
			{
				auto node = make("value");
				node->value = name == "true";
				return node;
			}
			auto node = make("call", name);
			require("(");
			if (m_token != ")")
			{
				for (;;)
				{
					node->children.push_back(parse(0, depth + 1));
					if (m_token != ",")
					{
						break;
					}
					next();
				}
			}
			require(")");
			return node;
		}
		fail("Unexpected expression token: " + m_token);
	}

	void next()
	{
		while (m_position < m_input.size() && m_input[m_position].isSpace())
		{
			++m_position;
		}
		m_token.clear();
		m_kind.clear();
		if (m_position == m_input.size())
		{
			return;
		}
		const auto character = m_input[m_position];
		if (character == '\'' || character == '"')
		{
			++m_position;
			QString value;
			while (m_position < m_input.size() && m_input[m_position] != character)
			{
				auto current = m_input[m_position++];
				if (current == '\\')
				{
					if (m_position == m_input.size())
					{
						fail("Incomplete string escape.");
					}
					current = m_input[m_position++];
					if (current == 'n')
					{
						current = '\n';
					}
				}
				value += current;
			}
			if (m_position == m_input.size())
			{
				fail("Unterminated expression string.");
			}
			++m_position;
			m_token = "string";
			m_kind = "value";
			m_value = value;
			return;
		}
		if (character.isDigit()
			|| (character == '.' && m_position + 1 < m_input.size() && m_input[m_position + 1].isDigit()))
		{
			static const QRegularExpression number("^(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)(?:[eE][+-]?[0-9]+)?");
			const auto match = number.match(m_input.mid(m_position));
			m_token = match.captured();
			m_position += m_token.size();
			bool valid = false;
			const auto value = m_token.toDouble(&valid);
			if (!valid || !std::isfinite(value))
			{
				fail("Expression contains an invalid number.");
			}
			m_kind = "value";
			m_value = value;
			return;
		}
		if (character.isLetter() || character == '_' || character == '$')
		{
			const auto start = m_position++;
			while (m_position < m_input.size())
			{
				const auto current = m_input[m_position];
				if (!current.isLetterOrNumber() && current != '_' && !(character == '$' && current == '.'))
				{
					break;
				}
				++m_position;
			}
			m_token = m_input.mid(start, m_position - start);
			m_kind = character == '$' ? "variable" : "name";
			return;
		}
		m_token = m_input.mid(m_position, 2);
		if (m_token == "&&" || m_token == "||" || m_token == "==" || m_token == "!=" || m_token == "<="
			|| m_token == ">=")
		{
			m_position += 2;
			return;
		}
		m_token = m_input.mid(m_position++, 1);
		if (!QString("+-*/%()[],!<>").contains(character))
		{
			fail("Unsupported expression character: " + m_token);
		}
	}

	QString m_input, m_token, m_kind;
	QJsonValue m_value;
	qsizetype m_position = 0;
	int m_nodes = 0;
};

QJsonValue evaluate(const Node& node, const QJsonObject& variables, std::uint32_t& state)
{
	if (node.kind == "value")
	{
		return node.value;
	}
	if (node.kind == "variable")
	{
		return variable(node.text, variables);
	}
	if (node.kind == "index")
	{
		const auto container = evaluate(*node.children[0], variables, state);
		const auto key = evaluate(*node.children[1], variables, state);
		if (container.isObject() && key.isString())
		{
			const auto result = container.toObject().value(key.toString());
			if (result.isUndefined())
			{
				fail("Unknown object field in expression.");
			}
			return result;
		}
		const auto index = scriptNumber(key);
		if (!container.isArray() || std::floor(index) != index || index < 0 || index >= container.toArray().size())
		{
			fail("Array index is out of range or is not an integer.");
		}
		return container.toArray()[static_cast<int>(index)];
	}
	if (node.kind == "array")
	{
		QJsonArray result;
		for (const auto& child : node.children)
		{
			result.append(evaluate(*child, variables, state));
		}
		return result;
	}
	if (node.kind == "unary")
	{
		const auto value = evaluate(*node.children[0], variables, state);
		if (node.text == "!")
		{
			return !scriptBoolean(value);
		}
		return scriptNumber(value) * (node.text == "-" ? -1.0 : 1.0);
	}
	if (node.kind == "binary")
	{
		const auto left = evaluate(*node.children[0], variables, state);
		if (node.text == "&&" && !scriptBoolean(left))
		{
			return false;
		}
		if (node.text == "||" && scriptBoolean(left))
		{
			return true;
		}
		const auto right = evaluate(*node.children[1], variables, state);
		if (node.text == "&&" || node.text == "||")
		{
			return scriptBoolean(right);
		}
		if (node.text == "==")
		{
			return left == right;
		}
		if (node.text == "!=")
		{
			return left != right;
		}
		const double a = scriptNumber(left), b = scriptNumber(right);
		if (node.text == "<")
		{
			return a < b;
		}
		if (node.text == ">")
		{
			return a > b;
		}
		if (node.text == "<=")
		{
			return a <= b;
		}
		if (node.text == ">=")
		{
			return a >= b;
		}
		if ((node.text == "/" || node.text == "%") && b == 0)
		{
			fail("Division by zero in expression.");
		}
		const double result = node.text == "+" ? a + b
			: node.text == "-"				   ? a - b
			: node.text == "*"				   ? a * b
			: node.text == "/"				   ? a / b
											   : std::fmod(a, b);
		if (!std::isfinite(result))
		{
			fail("Expression result is not finite.");
		}
		return result;
	}
	if (node.kind == "call")
	{
		if (node.text == "random" && node.children.size() == 2)
		{
			const double a = scriptNumber(evaluate(*node.children[0], variables, state));
			const double b = scriptNumber(evaluate(*node.children[1], variables, state));
			return scriptRandom(state, a, b);
		}
		if ((node.text == "pick" || node.text == "shuffle") && node.children.size() == 1)
		{
			const auto value = evaluate(*node.children[0], variables, state);
			if (!value.isArray() || value.toArray().isEmpty())
			{
				fail("pick/shuffle require a nonempty array.");
			}
			auto array = value.toArray();
			if (node.text == "pick")
			{
				return array[static_cast<int>(scriptRandom(state, 0, array.size()))];
			}
			for (int index = static_cast<int>(array.size()) - 1; index > 0; --index)
			{
				const int other = static_cast<int>(scriptRandom(state, 0, index + 1));
				const QJsonValue saved = array[index];
				array[index] = array[other];
				array[other] = saved;
			}
			return array;
		}
		fail("Unknown function or argument count: " + node.text);
	}
	fail("Invalid expression node.");
}

QJsonValue musicalLiteral(const QJsonValue& value, int ticksPerBar, const QString& argument)
{
	if (!value.isString())
	{
		return value;
	}
	const auto text = value.toString();
	if (argument == "key" || argument == "baseNote" || argument == "root")
	{
		static const QRegularExpression note("^([A-Ga-g])([#b]?)([0-9]{1,2})$");
		const auto match = note.match(text);
		if (match.hasMatch())
		{
			const auto letter = match.captured(1).toUpper();
			const int semitone = letter == "C" ? 0
				: letter == "D"				   ? 2
				: letter == "E"				   ? 4
				: letter == "F"				   ? 5
				: letter == "G"				   ? 7
				: letter == "A"				   ? 9
											   : 11;
			return match.captured(3).toInt() * 12 + semitone
				+ (match.captured(2) == "#"		   ? 1
						: match.captured(2) == "b" ? -1
												   : 0);
		}
	}
	if (QStringList{"position", "length", "start", "end", "ticks", "timing", "grid", "offset"}.contains(argument))
	{
		static const QRegularExpression bar("^bar:([0-9]+)(?:\\.([0-9]+))?$"), step("^step:([0-9]+)$"),
			duration("^1/([0-9]+)(t?)$");
		auto match = bar.match(text);
		if (match.hasMatch())
		{
			return match.captured(1).toDouble() * ticksPerBar + match.captured(2).toDouble() * 48;
		}
		match = step.match(text);
		if (match.hasMatch())
		{
			return match.captured(1).toDouble() * 12;
		}
		match = duration.match(text);
		if (match.hasMatch())
		{
			const double denominator = match.captured(1).toDouble();
			if (denominator <= 0)
			{
				fail("A note duration requires a positive denominator.");
			}
			return 192.0 / denominator * (match.captured(2).isEmpty() ? 1.0 : 2.0 / 3.0);
		}
	}
	return value;
}

QJsonValue resolve(const QJsonValue& value, const QJsonObject& variables, std::uint32_t& state, int ticksPerBar,
	const QString& argument, int depth)
{
	if (depth > 64)
	{
		fail("Script argument nesting exceeds 64 levels.");
	}
	if (value.isString())
	{
		const auto text = value.toString();
		return musicalLiteral(
			text.startsWith('$') ? evaluateScriptExpression(text, variables, state) : value, ticksPerBar, argument);
	}
	if (value.isArray())
	{
		const auto array = value.toArray();
		if (array.size() > 4096)
		{
			fail("Script arrays are limited to 4096 items.");
		}
		QJsonArray result;
		for (const auto& entry : array)
		{
			result.append(resolve(entry, variables, state, ticksPerBar, argument, depth + 1));
		}
		return result;
	}
	if (value.isObject())
	{
		const auto object = value.toObject();
		if (object.size() == 1 && object.contains("expr"))
		{
			if (!object.value("expr").isString())
			{
				fail("expr requires a string.");
			}
			return musicalLiteral(
				evaluateScriptExpression(object.value("expr").toString(), variables, state), ticksPerBar, argument);
		}
		QJsonObject result;
		for (auto it = object.begin(); it != object.end(); ++it)
		{
			result.insert(it.key(), resolve(it.value(), variables, state, ticksPerBar, it.key(), depth + 1));
		}
		return result;
	}
	return value;
}
}

double scriptNumber(const QJsonValue& value)
{
	if (!value.isDouble() || !std::isfinite(value.toDouble()))
	{
		fail("A finite numeric expression value is required.");
	}
	return value.toDouble();
}

bool scriptBoolean(const QJsonValue& value)
{
	if (value.isBool())
	{
		return value.toBool();
	}
	if (value.isDouble())
	{
		return scriptNumber(value) != 0;
	}
	fail("A boolean or numeric condition is required.");
}

double scriptRandom(std::uint32_t& state, double minimum, double maximum)
{
	if (!std::isfinite(minimum) || !std::isfinite(maximum) || minimum > maximum || !std::isfinite(maximum - minimum))
	{
		fail("Random bounds must be finite and ordered.");
	}
	state = state * 1664525u + 1013904223u;
	return minimum + (maximum - minimum) * (static_cast<double>(state) / 4294967296.0);
}

QJsonValue evaluateScriptExpression(const QString& expression, const QJsonObject& variables, std::uint32_t& state)
{
	Parser parser(expression);
	const auto tree = parser.parse();
	if (!parser.atEnd())
	{
		fail("Unexpected trailing expression input.");
	}
	return evaluate(*tree, variables, state);
}

QJsonValue evaluateScriptValue(const QJsonValue& value, const QJsonObject& variables, std::uint32_t& state,
	int ticksPerBar, const QString& argument)
{
	return resolve(value, variables, state, ticksPerBar, argument, 0);
}
}
