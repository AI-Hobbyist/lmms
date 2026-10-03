#include "CommandSchema.h"

#include <QJsonArray>
#include <QJsonValue>

#include <cmath>

namespace lmms::agent
{
namespace
{

QString validateValue( const QJsonObject &schema, const QJsonValue &value, const QString &path )
{
	for( const auto &keyword : { "anyOf", "oneOf" } )
	{
		if( !schema.contains( keyword ) ) { continue; }
		int matches = 0;
		for( const auto &alternative : schema.value( keyword ).toArray() )
		{
			if( validateValue( alternative.toObject(), value, path ).isEmpty() ) { ++matches; }
		}
		if( matches == 0 || ( QString::fromLatin1( keyword ) == "oneOf" && matches != 1 ) )
		{
			return QString( "%1 does not match the supported argument forms." ).arg( path );
		}
	}
	const auto type = schema.value( "type" ).toString();
	bool matches = true;
	if( type == "object" ) { matches = value.isObject(); }
	else if( type == "array" ) { matches = value.isArray(); }
	else if( type == "string" ) { matches = value.isString(); }
	else if( type == "boolean" ) { matches = value.isBool(); }
	else if( type == "null" ) { matches = value.isNull(); }
	else if( type == "number" || type == "integer" )
	{
		matches = value.isDouble() && std::isfinite( value.toDouble() ) &&
			( type != "integer" || std::floor( value.toDouble() ) == value.toDouble() );
	}
	if( !matches ) { return QString( "%1 must be %2." ).arg( path, type ); }
	if( schema.contains( "enum" ) && !schema.value( "enum" ).toArray().contains( value ) )
	{
		return QString( "%1 is not an allowed value." ).arg( path );
	}
	if( value.isDouble() )
	{
		if( schema.contains( "minimum" ) && value.toDouble() < schema.value( "minimum" ).toDouble() )
		{
			return QString( "%1 is below its minimum." ).arg( path );
		}
		if( schema.contains( "maximum" ) && value.toDouble() > schema.value( "maximum" ).toDouble() )
		{
			return QString( "%1 exceeds its maximum." ).arg( path );
		}
	}
	if( value.isObject() )
	{
		const auto object = value.toObject();
		for( const auto &required : schema.value( "required" ).toArray() )
		{
			if( !object.contains( required.toString() ) )
			{
				return QString( "%1.%2 is required." ).arg( path, required.toString() );
			}
		}
		const auto properties = schema.value( "properties" ).toObject();
		for( auto it = object.begin(); it != object.end(); ++it )
		{
			if( !properties.contains( it.key() ) )
			{
				if( schema.value( "additionalProperties" ).isBool() &&
					!schema.value( "additionalProperties" ).toBool() )
				{
					return QString( "%1.%2 is not a supported argument." ).arg( path, it.key() );
				}
				continue;
			}
			const auto error = validateValue( properties.value( it.key() ).toObject(), it.value(), path + "." + it.key() );
			if( !error.isEmpty() ) { return error; }
		}
	}
	if( value.isArray() )
	{
		const auto array = value.toArray();
		if( schema.contains( "maxItems" ) && array.size() > schema.value( "maxItems" ).toInt() )
		{
			return QString( "%1 contains too many items." ).arg( path );
		}
		for( int index = 0; schema.contains( "items" ) && index < array.size(); ++index )
		{
			const auto error = validateValue( schema.value( "items" ).toObject(), array[index],
				path + QString( "[%1]" ).arg( index ) );
			if( !error.isEmpty() ) { return error; }
		}
	}
	return {};
}

} // namespace

QJsonObject describeArguments( QJsonObject schema, bool mutating )
{
	schema.insert( "type", "object" );
	if( !schema.contains( "additionalProperties" ) ) { schema.insert( "additionalProperties", false ); }
	auto properties = schema.value( "properties" ).toObject();
	if( mutating )
	{
		properties.insert( "dryRun", QJsonObject{
			{ "type", "boolean" },
			{ "description", "Validate and preview the change without committing project or history changes." }
		} );
	}
	for( auto it = properties.begin(); it != properties.end(); ++it )
	{
		auto property = it.value().toObject();
		if( !property.contains( "description" ) )
		{
			property.insert( "description", QString( "The %1 argument for this command." ).arg( it.key() ) );
		}
		it.value() = property;
	}
	schema.insert( "properties", properties );
	return schema;
}

QString validateArguments( const QJsonObject &schema, const QJsonObject &arguments )
{
	if( arguments.contains( "dryRun" ) && !arguments.value( "dryRun" ).isBool() )
	{
		return "dryRun must be a boolean.";
	}
	return validateValue( schema, arguments, "arguments" );
}

} // namespace lmms::agent
