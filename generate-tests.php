#!/usr/bin/env php
<?php

/* vim: tabstop=4:softtabstop=4:shiftwidth=4:expandtab */

// Utils

function handlebarsc($tmpl, $op) {
    $return_var = 1;
    $output = array();
    $command = 'echo ' . escapeshellarg($tmpl) . ' | handlebarsc ' . escapeshellarg($op) . ' 2>&1';
    exec($command, $output, $return_var);
    if( $return_var == 127 ) {
        echo "handlebarsc is not available for printer testing!\n";
        exit(1);
    }
    return array(($return_var == 0), join("\n", $output), $command);
}

function patch_opcodes(array &$opcodes) {
    $childrenFn = function(&$children) {
        foreach( $children as $k => $v ) {
            patch_opcodes($children[$k]);
        }
        return $children;
    };
    $mainFn = function(&$main) use ($childrenFn) {
        foreach( $main as $k => $v ) {
            if( $k === 'options' ) {
                unset($main[$k]);
            } else if( $k === 'isSimple' || $k === 'guid' || $k === 'usePartial' || 
                       $k === 'trackIds' || $k === 'stringParams' || $k === 'depths' ) {
                // @todo add?
                unset($main[$k]);
            } else if( $k === 'children' ) {
                $childrenFn($main[$k]);
            }
        }
        return $main;
    };
    
    
    return $mainFn($opcodes);
}

function makeCompilerFlags(array $options = null)
{
    // Make flags
    $flags = 0;
    if( !empty($options['compat']) ) {
        $flags |= (1 << 0); //HANDLEBARS_COMPILER_FLAG_COMPAT;
    }
    if( !empty($options['stringParams']) ) {
        $flags |= (1 << 1); //HANDLEBARS_COMPILER_FLAG_STRING_PARAMS;
    }
    if( !empty($options['trackIds']) ) {
        $flags |= (1 << 2); //HANDLEBARS_COMPILER_FLAG_TRACK_IDS;
    }
    if( !empty($options['useDepths']) ) {
        $flags |= (1 << 0); //HANDLEBARS_COMPILER_FLAG_USE_DEPTHS;
    }
    if( !empty($options['knownHelpersOnly']) ) {
        $flags |= (1 << 4); //HANDLEBARS_COMPILER_FLAG_KNOWN_HELPERS_ONLY;
    }
    return $flags;
}

function token_print($tokens) {
    $str = '';
    foreach( $tokens as $token ) {
        $str .= sprintf('%s [%s] ', $token['name'], addcslashes($token['text'], "\t\r\n"));
    }
    return rtrim($str, ' ');
}

function hbs_test_file(array $test) {
    //$name = $test['it'] . '-' . $test['description'];
    //$safeName = strtolower(trim(preg_replace('/[^a-z0-9]+/i', '-', $name), '-'));
    $testFile = __DIR__ . '/tests' 
        . '/handlebars-' . $test['suiteType'] 
        . '/' . $test['suiteName'] 
        . '/' . sprintf("%03d", $test['number']) /*. '-' . $safeName */ . '.phpt';
    return $testFile;
}

function hbs_generate_test_head(array $test) {
    // Skip this test for now
    $skip = '';
    if( $test['number'] == 3 && $test['suiteName'] === 'basic' ) {
        $skip = 'true';
        $reason = 'skip for now'; 
    } else {
        $skip = "!extension_loaded('handlebars')";
        $reason = '';
    }
    
    $output = '';
    $output .= '--TEST--' . "\n";
    $output .= $test['suiteName'] . ' #' . $test['number'] . ' - ' . $test['it'] . "\n";
    $output .= '--DESCRIPTION--' . "\n";
    $output .= $test['description'] . "\n";
    $output .= '--SKIPIF--' . "\n";
    $output .= "<?php if( $skip ) die('skip $reason'); ?>" . "\n";
    $output .= '--FILE--' . "\n";
    $output .= '<?php' . "\n";
    //$output .= '$test = ' . var_export($test, true) . ';' . "\n";
    return $output;
}

function hbs_write_file($file, $contents) {
    // Make sure dir exists
    $dir = dirname($file);
    if( !is_dir($dir) ) {
        mkdir($dir, 0777, true);
    }
            
    // Write
    return file_put_contents($file, $contents);
}



// Test genenerator main

function hbs_generate_export_test_body(array $test) {
    
    $options = isset($test['options']) ? $test['options'] : array();
    $compileOptions = isset($test['compileOptions']) ? $test['compileOptions'] : array();
    $options += $compileOptions;
    
    $compileFlags = makeCompilerFlags($options);
    $knownHelpers = isset($options['knownHelpers']) ? $options['knownHelpers'] : array();
    /*if( isset($test['helpers']) ) {
        $knownHelpers = array_merge($knownHelpers, array_keys($test['helpers']));
    }
    if( isset($test['globalHelpers']) ) {
        $knownHelpers = array_merge($knownHelpers, array_keys($test['globalHelpers']));
    }*/
    if( empty($knownHelpers) ) {
        $knownHelpers = null;
    } else {
        $knownHelpers = array_keys($knownHelpers);
    }
    $expectedOpcodes = patch_opcodes($test['opcodes']);
    
    $output = '';
    $output .= '$options = ' . var_export($options, true) . ';' . PHP_EOL;
    $output .= '$tmpl = ' . var_export($test['template'], true) . ';' . PHP_EOL;
    $output .= '$compileFlags = ' . var_export($compileFlags, true) . ';' . PHP_EOL;
    $output .= '$knownHelpers = ' . var_export($knownHelpers, true) . ';' . PHP_EOL;
    $output .= 'var_export(handlebars_compile($tmpl, $compileFlags, $knownHelpers));' . PHP_EOL;
    $output .= '--EXPECT--' . PHP_EOL;
    $output .= var_export($expectedOpcodes, true) . PHP_EOL;
    return $output;
}

function hbs_generate_spec_test_body_tokenizer(array $test) {
    $output = '';
    $output .= '$tmpl = ' . var_export($test['template'], true) . ';' . PHP_EOL;
    $output .= 'var_export(handlebars_lex_print($tmpl));' . PHP_EOL;
    $output .= 'echo PHP_EOL;' . PHP_EOL;
    $output .= 'var_export(handlebars_lex($tmpl));' . PHP_EOL;
    $output .= '--EXPECT--' . PHP_EOL;
    $output .= var_export(token_print($test['expected']), true);
    $output .= PHP_EOL;
    $output .= var_export($test['expected'], true);
    return $output;
}

function hbs_generate_spec_test_body_parser(array $test) {
    if( empty($test['exception']) ) {
        $expected = rtrim($test['expected'], " \t\r\n");
    } else {
        $expected = false; //$test['message'];
    }
    
    $output = '';
    $output .= '$tmpl = ' . var_export($test['template'], true) . ';' . PHP_EOL;
    $output .= '$v = handlebars_parse_print($tmpl); var_export($v); // var_export(handlebars_error());' . PHP_EOL;
    $output .= 'echo PHP_EOL;' . PHP_EOL;
    $output .= 'var_export(gettype(handlebars_parse($tmpl)));' . PHP_EOL;
    $output .= '--EXPECT--' . PHP_EOL;
    $output .= var_export($expected, true) . PHP_EOL;
    if( empty($test['exception']) ) {
        $output .= var_export('array', true);
    } else {
        $output .= var_export('boolean', true);
    }
    //$output .= var_export($test['expected'], true);
    return $output;
}

function hbs_generate_spec_test_body(array $test) {
    switch( $test['suiteName'] ) {
        case 'parser':
            return hbs_generate_spec_test_body_parser($test);
            break;
        case 'tokenizer':
            return hbs_generate_spec_test_body_tokenizer($test);
            break;
        default:
            echo "Unknown spec: ", $test['suiteName'], PHP_EOL;
            exit(1);
            break;
    }
}

function hbs_generate_export_test(array $test) {
    $file = hbs_test_file($test);
    $body = hbs_generate_export_test_body($test);
    if( !$body ) {
        return;
    }
    
    $output = '';
    $output .= hbs_generate_test_head($test);
    $output .= $body;
    hbs_write_file($file, $output);
}

function hbs_generate_spec_test(array $test) {
    $file = hbs_test_file($test);
    $body = hbs_generate_spec_test_body($test);
    if( !$body ) {
        return;
    }
    
    $output = '';
    $output .= hbs_generate_test_head($test);
    $output .= $body;
    hbs_write_file($file, $output);
}



// Main

// Parser/Tokenizer
$specDir = __DIR__ . '/spec/handlebars/spec';
$parserSpecFile = $specDir . '/parser.json';
$tokenizerSpecFile = $specDir. '/tokenizer.json';

foreach( array($tokenizerSpecFile, $parserSpecFile) as $file ) {
    $suiteName = substr(basename($file), 0, strpos(basename($file), '.'));
    $tests = json_decode(file_get_contents($file), true);
    $number = 0;
    foreach( $tests as $test ) {
        ++$number;
        $test['suiteType'] = 'spec';
        $test['suiteName'] = $suiteName;
        $test['number'] = $number;
        hbs_generate_spec_test($test);
    }
}

// Export
$exportDir = __DIR__ . '/spec/handlebars/export/';
foreach( scandir($exportDir) as $file ) {
    if( $file[0] === '.' || substr($file, -5) !== '.json' ) {
        continue;
    }
    $filePath = $exportDir . $file;
    $fileName = basename($filePath);
    $suiteName = substr($fileName, 0, -strlen('.json'));
    $tests = json_decode(file_get_contents($filePath), true);

    if( !$tests ) {
        trigger_error("No tests in file: " . $file, E_USER_WARNING);
        continue;
    }

    $number = 0;
    foreach( $tests as $test ) {
        ++$number;
        $test['suiteType'] = 'export';
        $test['suiteName'] = $suiteName;
        $test['number'] = $number;
        hbs_generate_export_test($test);
    }
}
