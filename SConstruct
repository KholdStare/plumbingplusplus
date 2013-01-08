
env = Environment()  # Initialize the environment
env.Append(CPPFLAGS = [ '-O3', '-Wall', '-Werror', '-std=c++0x' ])
env.Append(LIBS = [ 'pthread',
            'boost_unit_test_framework' ] )

# debugging flags
debugflags = [ '-g', '-pg' ]
env.Append(CPPFLAGS =  debugflags)
env.Append(LINKFLAGS = debugflags)
	
commonSource = []
mainSource = ['plumbing_test.cpp' ]
testSource = ['test_suite.cpp']

mainSource.extend(commonSource)
testSource.extend(commonSource)

env.Program(target = 'plumbing_test', source = mainSource)
env.Program(target = 'test_suite', source = testSource)
