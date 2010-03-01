/*
 * This file is part of the Shiboken Python Bindings Generator project.
 *
 * Copyright (C) 2009-2010 Nokia Corporation and/or its subsidiary(-ies).
 *
 * Contact: PySide team <contact@pyside.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include "headergenerator.h"
#include <reporthandler.h>

#include <QtCore/QDir>
#include <QtCore/QTextStream>
#include <QtCore/QVariant>
#include <QtCore/QRegExp>
#include <QtCore/QDebug>

QString HeaderGenerator::fileNameForClass(const AbstractMetaClass* metaClass) const
{
    return metaClass->qualifiedCppName().toLower().replace("::", "_") + QLatin1String("_wrapper.h");
}

void HeaderGenerator::writeCopyCtor(QTextStream& s, const AbstractMetaClass* metaClass) const
{
    s << INDENT <<  wrapperName(metaClass) << "(const " << metaClass->qualifiedCppName() << "& self)";
    s << " : " << metaClass->qualifiedCppName() << "(self)" << endl;
    s << INDENT << "{" << endl;
    s << INDENT << "}" << endl << endl;
}

void HeaderGenerator::generateClass(QTextStream& s, const AbstractMetaClass* metaClass)
{
    ReportHandler::debugSparse("Generating header for " + metaClass->fullName());
    Indentation indent(INDENT);

    // write license comment
    s << licenseComment();

    QString wrapperName = HeaderGenerator::wrapperName(metaClass);
    QString headerGuard = wrapperName.replace("::", "_").toUpper();

    // Header
    s << "#ifndef SBK_" << headerGuard << "_H" << endl;
    s << "#define SBK_" << headerGuard << "_H" << endl<< endl;

#ifndef AVOID_PROTECTED_HACK
    s << "#define protected public" << endl << endl;
#endif

    s << "#include <shiboken.h>" << endl << endl;

    //Includes
    if (metaClass->typeEntry()->include().isValid())
        s << metaClass->typeEntry()->include().toString() << endl << endl;

    if (shouldGenerateCppWrapper(metaClass)) {

        if (usePySideExtensions() && metaClass->isQObject())
            s << "namespace PySide { class DynamicQMetaObject; }\n\n";

        // Class
        s << "class " << wrapperName;
        s << " : public " << metaClass->qualifiedCppName();

        s << endl << '{' << endl << "public:" << endl;

        if (isCopyable(metaClass))
            writeCopyCtor(s, metaClass);

        foreach (AbstractMetaFunction *func, filterFunctions(metaClass))
            writeFunction(s, func);

        //destructor
        s << INDENT << (metaClass->hasVirtualDestructor() ? "virtual " : "") << "~" << wrapperName << "();" << endl;

        writeCodeSnips(s, metaClass->typeEntry()->codeSnips(), CodeSnip::Declaration, TypeSystem::NativeCode);

        if (usePySideExtensions() && metaClass->isQObject()) {
            s << "public:\n";
            s << INDENT << "virtual int qt_metacall(QMetaObject::Call call, int id, void** args);\n";
            s << "private:\n";
            s << INDENT << "mutable PySide::DynamicQMetaObject* m_metaObject;\n";
        }

        s << "};" << endl << endl;
    }

    s << "#endif // SBK_" << headerGuard << "_H" << endl << endl;
}

void HeaderGenerator::writeFunction(QTextStream& s, const AbstractMetaFunction* func) const
{
    // do not write copy ctors here.
    if (func->isCopyConstructor())
        return;

    if (func->isConstructor() && func->isUserAdded())
        return;

#ifdef AVOID_PROTECTED_HACK
    if (func->isProtected() && !func->isConstructor()) {
        s << INDENT << "inline " << (func->isStatic() ? "static " : "");
        s << functionSignature(func, "", "_protected") << " { ";
        s << (func->type() ? "return " : "") << func->ownerClass()->qualifiedCppName() << "::";
        writeFunctionCall(s, func);
        s << "; }" << endl;
    }
#endif

    // pure virtual functions need a default implementation
    if (func->isPrivate() || (func->isModifiedRemoved() && !func->isAbstract()))
        return;

    if (func->isConstructor() || func->isAbstract() || func->isVirtual()) {
        s << INDENT;
        Options virtualOption = Generator::NoOption;
        if (func->isVirtual() || func->isAbstract()) {
            virtualOption = Generator::OriginalTypeDescription;
            s << "virtual ";
        }
        s << functionSignature(func, "", "", virtualOption) << ';' << endl;

        // TODO: when modified an abstract method ceases to be virtual but stays abstract
        //if (func->isModifiedRemoved() && func->isAbstract()) {
        //}
    }
}

void HeaderGenerator::writeTypeCheckMacro(QTextStream& s, const TypeEntry* type)
{
    QString pyTypeName = cppApiVariableName() + '[' + getTypeIndexVariableName(type) + ']';
    QString checkFunction = cpythonCheckFunction(type);
    s << "#define " << checkFunction << "(op) PyObject_TypeCheck(op, (PyTypeObject*)";
    s << pyTypeName << ')' << endl;
    s << "#define " << checkFunction << "Exact(op) ((op)->ob_type == (PyTypeObject*)";
    s << pyTypeName << ')' << endl;
}

void HeaderGenerator::writeTypeConverterDecl(QTextStream& s, const TypeEntry* type)
{
    s << "template<>" << endl;

    const AbstractMetaClass* metaClass = classes().findClass(type->name());
    bool isAbstractOrObjectType = (metaClass &&  metaClass->isAbstract()) || type->isObject();

    AbstractMetaFunctionList implicitConvs;
    foreach (AbstractMetaFunction* func, implicitConversions(type)) {
        if (!func->isUserAdded())
            implicitConvs << func;
    }
    bool isValueTypeWithImplConversions = type->isValue() && !implicitConvs.isEmpty();

    s << "struct Converter<" << type->name() << (isAbstractOrObjectType ? "*" : "") << " > : ";
    if (type->isEnum() || type->isFlags())
        s << "Converter_CppEnum";
    else
        s << "ConverterBase";
    s << '<' << type->name() << (isAbstractOrObjectType ? "*" : "") << " >" << endl;
    s << '{' << endl;
    if (isValueTypeWithImplConversions) {
        s << INDENT << "static " << type->name() << " toCpp(PyObject* pyobj);" << endl;
        s << INDENT << "static bool isConvertible(PyObject* pyobj);" << endl;
    }
    s << "};" << endl;
}

void HeaderGenerator::writeTypeIndexDefineLine(QTextStream& s, const TypeEntry* typeEntry, int& idx)
{
    if (!typeEntry || !typeEntry->generateCode())
        return;
    s.setFieldAlignment(QTextStream::AlignLeft);
    s << "#define ";
    s.setFieldWidth(60);
    s << getTypeIndexVariableName(typeEntry);
    s.setFieldWidth(0);
    s << ' ' << (idx++) << endl;
    if (typeEntry->isEnum()) {
        const EnumTypeEntry* ete = reinterpret_cast<const EnumTypeEntry*>(typeEntry);
        if (ete->flags())
            writeTypeIndexDefineLine(s, ete->flags(), idx);
    }
}

void HeaderGenerator::writeTypeIndexDefine(QTextStream& s, const AbstractMetaClass* metaClass, int& idx)
{
    if (!metaClass->typeEntry()->generateCode())
        return;
    if (!metaClass->isNamespace())
        writeTypeIndexDefineLine(s, metaClass->typeEntry(), idx);
    foreach (const AbstractMetaEnum* metaEnum, metaClass->enums())
        writeTypeIndexDefineLine(s, metaEnum->typeEntry(), idx);
}

void HeaderGenerator::finishGeneration()
{
    if (classes().isEmpty())
        return;

    // Generate the main header for this module.
    // This header should be included by binding modules
    // extendind on top of this one.
    QString classIncludes;
    QTextStream s_cin(&classIncludes);
    QSet<QString> enumIncludes;
    QString pythonTypeStuff;
    QTextStream s_pts(&pythonTypeStuff);
    QString convertersDecl;
    QTextStream convDecl(&convertersDecl);
    QString sbkTypeFunctions;
    QTextStream typeFunctions(&sbkTypeFunctions);
    QString converterImpl;
    QTextStream convImpl(&converterImpl);

    Indentation indent(INDENT);

    s_pts << "// Type indices" << endl;
    int idx = 0;
    foreach (const AbstractMetaClass* metaClass, classes())
        writeTypeIndexDefine(s_pts, metaClass, idx);
    foreach (const AbstractMetaEnum* metaEnum, globalEnums())
        writeTypeIndexDefineLine(s_pts, metaEnum->typeEntry(), idx);
    s_pts << "#define ";
    s_pts.setFieldWidth(60);
    s_pts << "SBK_"+moduleName()+"_IDX_COUNT";
    s_pts.setFieldWidth(0);
    s_pts << ' ' << idx << endl << endl;
    s_pts << "// This variable stores all python types exported by this module" << endl;
    s_pts << "extern PyTypeObject** " << cppApiVariableName() << ';' << endl << endl;

    s_pts << "// Useful macros" << endl;
    foreach (const AbstractMetaEnum* cppEnum, globalEnums()) {
        QString incFile = cppEnum->includeFile().split(QDir::separator()).takeLast();
        if (!incFile.isEmpty())
            enumIncludes << cppEnum->includeFile();
        writeTypeCheckMacro(s_pts, cppEnum->typeEntry());
        FlagsTypeEntry* flags = cppEnum->typeEntry()->flags();
        if (flags)
            writeTypeCheckMacro(s_pts, flags);
        s_pts << endl;
        writeTypeConverterDecl(convDecl, cppEnum->typeEntry());
        convDecl << endl;
        writeSbkTypeFunction(typeFunctions, cppEnum);
    }

    foreach (AbstractMetaClass* metaClass, classes()) {
        const TypeEntry* classType = metaClass->typeEntry();
        if (!shouldGenerate(metaClass) || metaClass->enclosingClass() ||
            !(classType->isObject() || classType->isValue() || classType->isNamespace()))
            continue;

        //Includes
        if (metaClass->typeEntry()->include().isValid())
            s_cin << metaClass->typeEntry()->include().toString() << endl;

        foreach (const AbstractMetaEnum* cppEnum, metaClass->enums()) {
            writeTypeCheckMacro(s_pts, cppEnum->typeEntry());
            writeTypeConverterDecl(convDecl, cppEnum->typeEntry());
            FlagsTypeEntry* flagsEntry = cppEnum->typeEntry()->flags();
            if (flagsEntry) {
                writeTypeCheckMacro(s_pts, flagsEntry);
                writeTypeConverterDecl(convDecl, flagsEntry);
            }
            s_pts << endl;
            convDecl << endl;
            writeSbkTypeFunction(typeFunctions, cppEnum);
        }

        if (!metaClass->isNamespace()) {
            writeSbkTypeFunction(typeFunctions, metaClass);

            writeSbkCopyCppObjectFunction(convDecl, metaClass);

            foreach (AbstractMetaClass* innerClass, metaClass->innerClasses()) {
                if (shouldGenerate(innerClass)) {
                    writeSbkCopyCppObjectFunction(convDecl, innerClass);
                    s_cin << innerClass->typeEntry()->include().toString() << endl;
                    writeTypeCheckMacro(s_pts, innerClass->typeEntry());
                    writeTypeConverterDecl(convDecl, innerClass->typeEntry());
                    writeTypeConverterImpl(convImpl, innerClass->typeEntry());
                    convDecl << endl;
                    writeSbkTypeFunction(typeFunctions, innerClass);
                }
            }
            writeTypeCheckMacro(s_pts, classType);
            writeTypeConverterDecl(convDecl, classType);
            writeTypeConverterImpl(convImpl, classType);
            convDecl << endl;
        }
    }

    QString moduleHeaderFileName(outputDirectory()
                                 + QDir::separator() + subDirectoryForPackage(packageName())
                                 + QDir::separator() + getModuleHeaderFileName());

    QString includeShield("SBK_" + moduleName().toUpper() + "_PYTHON_H");

    QFile file(moduleHeaderFileName);
    if (file.open(QFile::WriteOnly)) {
        QTextStream s(&file);

        // write license comment
        s << licenseComment() << endl << endl;

        s << "#ifndef " << includeShield << endl;
        s << "#define " << includeShield << endl<< endl;
        #ifndef AVOID_PROTECTED_HACK
        s << "//workaround to access protected functions" << endl;
        s << "#define protected public" << endl << endl;
        #endif

        s << "#include <Python.h>" << endl;
        s << "#include <conversions.h>" << endl;
        s << "#include <pyenum.h>" << endl;
        s << "#include <basewrapper.h>" << endl;
        s << "#include <bindingmanager.h>" << endl << endl;

        s << "#include <memory>" << endl << endl;
        writeExportMacros(s);

        QStringList requiredTargetImports = TypeDatabase::instance()->requiredTargetImports();
        if (!requiredTargetImports.isEmpty()) {
            s << "// Module Includes" << endl;
            foreach (const QString& requiredModule, requiredTargetImports)
                s << "#include <" << getModuleHeaderFileName(requiredModule) << ">" << endl;
            s << endl;
        }

        s << "// Class Includes" << endl;
        s << classIncludes << endl;

        if (!enumIncludes.isEmpty()) {
            s << "// Enum Includes" << endl;
            foreach (const QString& include, enumIncludes)
              s << "#include <" << include << ">" << endl;
            s << endl;
        }

        if (!primitiveTypes().isEmpty()) {
            s << "// Conversion Includes - Primitive Types" << endl;
            foreach (const PrimitiveTypeEntry* ptype, primitiveTypes()) {
                if (ptype->include().isValid())
                    s << ptype->include().toString() << endl;
            }
            s << endl;
        }

        if (!containerTypes().isEmpty()) {
            s << "// Conversion Includes - Container Types" << endl;
            foreach (const ContainerTypeEntry* ctype, containerTypes()) {
                if (ctype->include().isValid())
                    s << ctype->include().toString() << endl;
            }
            s << endl;
        }

        s << "extern \"C\"" << endl << '{' << endl << endl;
        s << pythonTypeStuff << endl;
        s << "} // extern \"C\"" << endl << endl;

        s << "namespace Shiboken" << endl << '{' << endl << endl;

        s << "// PyType functions, to get the PyObjectType for a type T\n";
        s << sbkTypeFunctions << endl;
        s << "// Generated converters declarations ----------------------------------" << endl << endl;
        s << convertersDecl << endl;
        s << "} // namespace Shiboken" << endl << endl;

        s << "// User defined converters --------------------------------------------" << endl;
        foreach (TypeEntry* typeEntry, TypeDatabase::instance()->entries()) {
            if (typeEntry->hasConversionRule()) {
                s << "// Conversion rule for: " << typeEntry->name() << endl;
                s << typeEntry->conversionRule();
            }
        }
        s << "// Generated converters implemantations -------------------------------" << endl << endl;
        s << converterImpl << endl;

        s << "#endif // " << includeShield << endl << endl;
    }
}


void HeaderGenerator::writeExportMacros(QTextStream& s)
{
    QString macro = getApiExportMacro();
    s << "\
#if defined _WIN32 || defined __CYGWIN__\n\
    #define " << macro << " __declspec(dllexport)\n\
#else\n\
#if __GNUC__ >= 4\n\
    #define " << macro << " __attribute__ ((visibility(\"default\")))\n\
#else\n\
    #define " << macro << "\n\
#endif\n\
#endif\n\
\n";
}

void HeaderGenerator::writeSbkTypeFunction(QTextStream& s, const AbstractMetaEnum* cppEnum)
{
    QString enumPrefix;
    if (cppEnum->enclosingClass())
        enumPrefix = cppEnum->enclosingClass()->qualifiedCppName() + "::";
    s <<  "template<>\ninline PyTypeObject* SbkType<" << enumPrefix << cppEnum->name() << " >() "
      << "{ return " << cpythonTypeNameExt(cppEnum->typeEntry()) << "; }\n";

    FlagsTypeEntry* flag = cppEnum->typeEntry()->flags();
    if (flag) {
        s <<  "template<>\ninline PyTypeObject* SbkType<" << flag->name() << " >() "
          << "{ return " << cpythonTypeNameExt(flag) << "; }\n";
    }
}

void HeaderGenerator::writeSbkTypeFunction(QTextStream& s, const AbstractMetaClass* cppClass)
{
    s <<  "template<>\ninline PyTypeObject* SbkType<" << cppClass->qualifiedCppName() << " >() "
      <<  "{ return reinterpret_cast<PyTypeObject*>(" << cpythonTypeNameExt(cppClass->typeEntry()) << "); }\n";
}

void HeaderGenerator::writeSbkCopyCppObjectFunction(QTextStream& s, const AbstractMetaClass* metaClass)
{
    if (!metaClass->typeEntry()->isValue() || !shouldGenerateCppWrapper(metaClass))
        return;
    QString className = metaClass->qualifiedCppName();
    s << "template <>" << endl;
    s << "struct SbkTypeInfo<" << className << " >" << endl;
    s << '{' << endl;
    s << INDENT << "static const bool isCppWrapper = true;" << endl;
    s << "};" << endl;
}

void HeaderGenerator::writeTypeConverterImpl(QTextStream& s, const TypeEntry* type)
{
    if (type->hasConversionRule())
        return;

    QString pyTypeName = cpythonTypeName(type);

    AbstractMetaFunctionList implicitConvs;
    foreach (AbstractMetaFunction* func, implicitConversions(type)) {
        if (!func->isUserAdded())
            implicitConvs << func;
    }

    bool hasImplicitConversions = !implicitConvs.isEmpty();

    // A specialized Converter<T>::toCpp method is only need for
    // classes with implicit conversions.
    if (!hasImplicitConversions)
        return;

    // Write Converter<T>::isConvertible
    s << "inline bool Shiboken::Converter<" << type->name() << " >::isConvertible(PyObject* pyobj)" << endl;
    s << '{' << endl;
    s << INDENT << "return ";
    bool isFirst = true;
    foreach (const AbstractMetaFunction* ctor, implicitConvs) {
        Indentation indent(INDENT);
        if (isFirst)
            isFirst = false;
        else
            s << endl << INDENT << " || ";
        if (ctor->isConversionOperator())
            s << cpythonCheckFunction(ctor->ownerClass()->typeEntry());
        else
            s << cpythonCheckFunction(ctor->arguments().first()->type());
        s << "(pyobj)";
    }
    s << ';' << endl;
    s << '}' << endl << endl;

    // Write Converter<T>::toCpp function
    s << "inline " << type->name() << " Shiboken::Converter<" << type->name() << " >::toCpp(PyObject* pyobj)" << endl;
    s << '{' << endl;

    s << INDENT << "if (!Shiboken_TypeCheck(pyobj, " << type->name() << ")) {" << endl;
    bool firstImplicitIf = true;
    foreach (const AbstractMetaFunction* ctor, implicitConvs) {
        if (ctor->isModifiedRemoved())
            continue;

        Indentation indent(INDENT);
        s << INDENT;
        if (firstImplicitIf)
            firstImplicitIf = false;
        else
            s << "else ";

        QString typeCheck;
        QString toCppConv;
        QTextStream tcc(&toCppConv);
        if (ctor->isConversionOperator()) {
            const AbstractMetaClass* metaClass = ctor->ownerClass();
            typeCheck = cpythonCheckFunction(metaClass->typeEntry());
            writeToCppConversion(tcc, metaClass, "pyobj");
        } else {
            const AbstractMetaType* argType = ctor->arguments().first()->type();
            typeCheck = cpythonCheckFunction(argType);
            writeToCppConversion(tcc, argType, 0, "pyobj");
        }

        s << "if (" << typeCheck << "(pyobj))" << endl;
        {
            Indentation indent(INDENT);
            s << INDENT << "return " << type->name() << '(' << toCppConv << ");" << endl;
        }
    }
    s << INDENT << '}' << endl;

    s << INDENT << "return *" << cpythonWrapperCPtr(type, "pyobj") << ';' << endl;
    s << '}' << endl << endl;
}

